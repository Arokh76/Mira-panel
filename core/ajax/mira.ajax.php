<?php
/* This file is part of Jeedom.
 *
 * Jeedom is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

try {
    require_once dirname(__FILE__) . '/../../../../core/php/core.inc.php';
    include_file('core', 'authentification', 'php');

    if (!isConnect('admin')) {
        throw new Exception(__('401 - Accès non autorisé', __FILE__));
    }

    ajax::init();

    if (init('action') == 'SyncByScreen') {
        /*
         * IMPORTANT : /savelistlvgl repond "OK", attend ~500 ms puis redemarre
         * l'ESP32. Les anciens patchs synchronisaient Jeedom juste apres le OK :
         * ils relisaient donc souvent l'ANCIEN /getindex, ce qui explique que le
         * clic manuel sur "Sauvegarder" fonctionnait quelques secondes plus tard.
         *
         * Ici on laisse le reboot commencer, puis on poll /getindex jusqu'a ce que
         * l'index corresponde vraiment a la configuration qui vient d'etre envoyee.
         */
        ignore_user_abort(true);
        @set_time_limit(35);
        if (function_exists('session_status') && session_status() === PHP_SESSION_ACTIVE) {
            @session_write_close();
        }

        $ip = trim((string)init('ip'));
        if ($ip == '') {
            throw new Exception(__('Adresse IP manquante', __FILE__));
        }

        $normalizeIp = function ($value) {
            $value = trim((string)$value);
            $value = preg_replace('#^https?://#i', '', $value);
            return rtrim($value, '/');
        };
        $wantedIp = $normalizeIp($ip);

        $expected = json_decode((string)init('expected'), true);
        if (!is_array($expected)) $expected = array();

        /* Verifie que /getindex correspond EXACTEMENT a la configuration
         * envoyee. C'est important pour une suppression : l'ancien index est un
         * sur-ensemble du nouveau et pouvait auparavant etre valide trop tot.
         * datetime ne cree pas d'entree dans /getindex, roundrange devient range
         * et list/listbutton deviennent une entree type=list par option.
         */
        $indexMatchesExpected = function ($actions, $widgets) {
            if (!is_array($actions)) return false;

            $expectedSignatures = array();
            foreach ($widgets as $widget) {
                if (!is_array($widget)) continue;
                $type = isset($widget['type']) ? trim((string)$widget['type']) : '';
                $name = isset($widget['name']) ? trim((string)$widget['name']) : '';
                $value = isset($widget['value']) ? (string)$widget['value'] : '';

                if ($type == '' || $type == 'datetime') continue;

                if ($type === 'thermostat') {
                    // One physical card, but three standard Mira entries in
                    // /getindex. Keep exact expected signatures so autosync
                    // cannot accidentally validate an old index.
                    $expectedSignatures[] = 'info|' . $name . '__temperature';
                    $expectedSignatures[] = 'range|' . $name . '__consigne';
                    $modes = isset($widget['modes']) ? (string)$widget['modes'] : 'Confort|Éco|Hors gel|Arrêt';
                    foreach (explode('|', $modes) as $option) {
                        $option = trim($option);
                        if ($option !== '') $expectedSignatures[] = 'list|' . $name . '__mode|' . $option;
                    }
                    continue;
                }

                if ($type == 'list' || $type == 'listbutton') {
                    foreach (explode('|', $value) as $option) {
                        $option = trim($option);
                        if ($option === '') continue;
                        $expectedSignatures[] = 'list|' . $name . '|' . $option;
                    }
                    continue;
                }

                $indexType = ($type == 'roundrange') ? 'range' : $type;
                /* Les types shield restent ignores tant que le firmware WT32 ne
                 * les genere pas dans son index. */
                if (!in_array($indexType, array('info', 'button', 'toggle', 'range', 'color'), true)) {
                    continue;
                }
                $expectedSignatures[] = $indexType . '|' . $name;
            }

            $actualSignatures = array();
            foreach ($actions as $action) {
                if (!is_array($action)) continue;
                $actionType = trim((string)($action['type'] ?? ''));

                if ($actionType === 'list') {
                    $listName = trim((string)($action['nomliste'] ?? ''));
                    $itemName = trim((string)($action['nom'] ?? ''));
                    if ($listName !== '' && $itemName !== '') {
                        $actualSignatures[] = 'list|' . $listName . '|' . $itemName;
                    }
                    continue;
                }

                if (!in_array($actionType, array('info', 'button', 'toggle', 'range', 'color'), true)) {
                    continue;
                }
                $actionName = trim((string)($action['nom'] ?? ''));
                if ($actionName !== '') {
                    $actualSignatures[] = $actionType . '|' . $actionName;
                }
            }

            sort($expectedSignatures, SORT_STRING);
            sort($actualSignatures, SORT_STRING);
            return $expectedSignatures === $actualSignatures;
        };

        $results = array();
        $matchedEq = 0;

        foreach (eqLogic::byType('mira') as $eqLogic) {
            if ($normalizeIp($eqLogic->getConfiguration('adresseip')) !== $wantedIp) continue;
            $matchedEq++;

            $password = (string)$eqLogic->getConfiguration('password');
            $url = 'http://' . $wantedIp . '/getindex?key=' . rawurlencode($password);

            log::add('mira', 'debug', '[AutoSync] attente du nouvel index de ' . $wantedIp);

            /* Le handler ESP32 redemarre 500 ms apres savelistlvgl. On attend
             * volontairement 2 s avant le premier test pour ne jamais valider
             * l'ancien index juste avant le reboot. */
            usleep(2000000);

            $deadline = microtime(true) + 22.0;
            $ready = false;
            $attempts = 0;
            $lastState = 'offline';

            while (microtime(true) < $deadline) {
                $attempts++;
                $ch = curl_init();
                curl_setopt($ch, CURLOPT_URL, $url);
                curl_setopt($ch, CURLOPT_RETURNTRANSFER, 1);
                curl_setopt($ch, CURLOPT_CONNECTTIMEOUT, 1);
                curl_setopt($ch, CURLOPT_TIMEOUT, 2);
                $raw = curl_exec($ch);
                $errno = curl_errno($ch);
                $httpCode = (int)curl_getinfo($ch, CURLINFO_HTTP_CODE);
                curl_close($ch);

                if ($errno === 0 && $httpCode >= 200 && $httpCode < 300 && is_string($raw) && $raw !== '') {
                    $decoded = json_decode($raw, true);
                    if (is_array($decoded) && isset($decoded['actions']) && is_array($decoded['actions'])) {
                        $lastState = 'index reçu';
                        if ($indexMatchesExpected($decoded['actions'], $expected)) {
                            $ready = true;
                            break;
                        }
                        $lastState = 'ancien index';
                    } else {
                        $lastState = 'json invalide';
                    }
                } else {
                    $lastState = 'ESP indisponible';
                }

                usleep(750000);
            }

            if (!$ready) {
                log::add('mira', 'warning', '[AutoSync] timeout pour ' . $wantedIp . ' (' . $lastState . ', ' . $attempts . ' essais)');
                $results[] = array(
                    'id' => $eqLogic->getId(),
                    'status' => 'timeout',
                    'attempts' => $attempts,
                    'lastState' => $lastState
                );
                continue;
            }

            try {
                /* Maintenant seulement, l'ecran expose le nouvel index. postUpdate
                 * cree/met a jour les commandes puis reinjecte les IDs Jeedom dans
                 * l'ESP32 comme lors d'un clic manuel sur Sauvegarder. */
                $eqLogic->postUpdate();
                log::add('mira', 'debug', '[AutoSync] commandes synchronisees pour ' . $wantedIp . ' apres ' . $attempts . ' essais');
                $results[] = array(
                    'id' => $eqLogic->getId(),
                    'status' => 'synced',
                    'attempts' => $attempts
                );
            } catch (Exception $syncException) {
                log::add('mira', 'error', '[AutoSync] erreur postUpdate: ' . $syncException->getMessage());
                $results[] = array(
                    'id' => $eqLogic->getId(),
                    'status' => 'error',
                    'message' => $syncException->getMessage()
                );
            }
        }

        ajax::success(array(
            'count' => $matchedEq,
            'results' => $results
        ));
    }

    /*
     * /programui peut enregistrer les widgets, puis redemarrer l'ESP avant
     * d'avoir remis son HTTP 200 au client. Un timeout cURL NE signifie PAS
     * automatiquement un echec. On relit le JSON reel de /getlvgl et on
     * confirme l'ensemble des widgets avant de repondre a Jeedom.
     * Aucun POST rejoue : evite les doubles creations et les effets de bord.
     */
    $miraProgrammingMatches = function ($sent, $received) {
        if (!is_array($sent) || !isset($sent['lvgl']) || !is_array($sent['lvgl']) ||
            !is_array($received) || !isset($received['lvgl']) || !is_array($received['lvgl'])) {
            return false;
        }
        if (count($sent['lvgl']) !== count($received['lvgl'])) return false;
        foreach ($sent['lvgl'] as $i => $widget) {
            if (!is_array($widget) || !isset($received['lvgl'][$i]) ||
                !is_array($received['lvgl'][$i])) return false;
            $actual = $received['lvgl'][$i];
            /* Contrat de programmation : comparaison de TOUTES les proprietes
             * envoyees, a l'exception des champs techniques rajoutes par Mira.
             * Mini/Max, disposition, icone, valeurs et nom affiche sont inclus. */
            foreach ($widget as $field => $value) {
                if ($field === 'displayName') {
                    $wanted = (string)$value;
                    $got = isset($actual['displayName']) ? (string)$actual['displayName'] :
                        (string)($actual['name'] ?? '');
                    if ($got !== $wanted) return false;
                    continue;
                }
                if (!array_key_exists($field, $actual)) {
                    // Firmware historique : champ optionnel laisse a sa valeur implicite.
                    $defaults = array('layout' => 'auto', 'style' => 'standard',
                                      'labelMode' => 'auto', 'icon' => 'auto');
                    if (!array_key_exists($field, $defaults) || (string)$value !== $defaults[$field]) {
                        return false;
                    }
                    continue;
                }
                if (is_array($value) || is_object($value) || is_array($actual[$field])) {
                    if (json_encode($value) !== json_encode($actual[$field])) return false;
                } else if ((string)$value !== (string)$actual[$field]) {
                    return false;
                }
            }
            /* Un displayName absent signifie qu'il faut afficher name. Ce
             * controle empeche un ancien libelle persistant d'etre valide. */
            if (!array_key_exists('displayName', $widget) &&
                isset($actual['displayName']) && (string)$actual['displayName'] !== '' &&
                (string)$actual['displayName'] !== (string)($widget['name'] ?? '')) {
                return false;
            }
        }
        return true;
    };

    $miraVerifyProgramUI = function ($host, $key, $expected, $waitSeconds) use ($miraProgrammingMatches) {
        $readUrl = 'http://' . $host . '/getlvgl?key=' . rawurlencode($key);
        $deadline = microtime(true) + $waitSeconds;
        $attempts = 0;
        $lastReason = 'pas de reponse';
        while (microtime(true) < $deadline) {
            $attempts++;
            $read = curl_init();
            curl_setopt($read, CURLOPT_URL, $readUrl);
            curl_setopt($read, CURLOPT_RETURNTRANSFER, true);
            curl_setopt($read, CURLOPT_CONNECTTIMEOUT, 1);
            curl_setopt($read, CURLOPT_TIMEOUT, 3);
            $raw = curl_exec($read);
            $errno = curl_errno($read);
            $code = (int)curl_getinfo($read, CURLINFO_HTTP_CODE);
            curl_close($read);
            if (!$errno && $code === 200 && is_string($raw) && $raw !== '') {
                $decoded = json_decode($raw, true);
                if ($miraProgrammingMatches($expected, $decoded)) {
                    return array('ok' => true, 'attempts' => $attempts, 'lastReason' => 'configuration verifiee');
                }
                $lastReason = is_array($decoded) ? 'configuration differente' : 'JSON illisible';
            } else {
                $lastReason = 'ecran indisponible (HTTP ' . $code . ')';
            }
            usleep(650000);
        }
        return array('ok' => false, 'attempts' => $attempts, 'lastReason' => $lastReason);
    };

    if (init('action') == 'PostHTTPJson') {
        $url = trim((string)init('url'));
        $key = (string)init('key');
        $json = (string)init('json');
        $parts = parse_url($url);
        if (!$parts || !isset($parts['scheme'], $parts['host']) ||
            strtolower($parts['scheme']) !== 'http' ||
            ($parts['path'] ?? '') !== '/programui') {
            throw new Exception('Mira PostHTTPJson : adresse /programui invalide.');
        }
        $expected = json_decode($json, true);
        if (!is_array($expected) || !isset($expected['lvgl']) || !is_array($expected['lvgl'])) {
            throw new Exception('Mira PostHTTPJson : configuration LVGL absente ou invalide.');
        }
        @set_time_limit(45);
        if (function_exists('session_status') && session_status() === PHP_SESSION_ACTIVE) {
            @session_write_close();
        }
        $host = $parts['host'];
        // 12 s maximum pour l'accuse-reception, suivi si besoin d'une
        // verification en lecture seule. Aucun nouvel envoi n'est effectue.
        log::add('mira', 'debug', '[ProgramUI] Envoi vers ' . $host .
            ' (' . strlen($json) . ' octets JSON, accusé attendu 12 s)');
        $ch = curl_init();
        curl_setopt($ch, CURLOPT_URL, $url);
        curl_setopt($ch, CURLOPT_POST, true);
        curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);
        curl_setopt($ch, CURLOPT_CONNECTTIMEOUT, 3);
        curl_setopt($ch, CURLOPT_TIMEOUT, 12);
        curl_setopt($ch, CURLOPT_HTTPHEADER, array('Content-Type: application/x-www-form-urlencoded'));
        curl_setopt($ch, CURLOPT_POSTFIELDS, http_build_query(array('key' => $key, 'json' => $json)));
        $result = curl_exec($ch);
        $httpCode = (int)curl_getinfo($ch, CURLINFO_HTTP_CODE);
        $curlErrno = curl_errno($ch);
        $curlError = $curlErrno ? curl_error($ch) : '';
        curl_close($ch);

        $body = is_string($result) ? $result : '';
        if (!$curlErrno && $httpCode === 200 && trim($body) === 'OK') {
            log::add('mira', 'debug', '[ProgramUI] Accusé HTTP 200 OK de ' . $host);
            ajax::success(array('transport' => 'mira-1.2.17', 'httpCode' => 200,
                                'body' => 'OK', 'verifiedBy' => 'ack'));
        }
        // Si l'ESP a explicitement rejete le POST (erreur JSON, mauvaise cle),
        // le retour doit etre montre : ne JAMAIS masquer ce refus.
        if (!$curlErrno && $httpCode > 0) {
            log::add('mira', 'warning', '[ProgramUI] Rejet explicite par ' . $host .
                ': HTTP ' . $httpCode . ', ' . strlen($body) . ' octets');
            ajax::success(array('transport' => 'mira-1.2.17', 'httpCode' => $httpCode,
                                'body' => $body, 'verifiedBy' => 'rejected'));
        }

        // L'ESP a ferme sa connexion ou n'a jamais repondu. Relire le
        // resultat persistant de /getlvgl pour distinguer reboot et echec.
        log::add('mira', 'warning', '[ProgramUI] Pas d’accusé de ' . $host .
            ' (' . $curlError . '), verification de /getlvgl');
        $verified = $miraVerifyProgramUI($host, $key, $expected, 17.0);
        if ($verified['ok']) {
            log::add('mira', 'debug', '[ProgramUI] Configuration relue et identique sur ' .
                $host . ' après ' . $verified['attempts'] . ' essai(s)');
            ajax::success(array('transport' => 'mira-1.2.17', 'httpCode' => 200,
                                'body' => 'OK', 'verifiedBy' => 'getlvgl'));
        }
        log::add('mira', 'warning', '[ProgramUI] Configuration non confirmee sur ' .
            $host . ' (' . $verified['lastReason'] . ')');
        throw new Exception('Mira /programui : ' . $curlError .
            '. Vérification de /getlvgl impossible (' . $verified['lastReason'] .
            ', ' . $verified['attempts'] . ' essais).');
    }

    if (init('action') == 'SyncNavigationFavorites') {
        $id = (int)init('id');
        $eqLogic = eqLogic::byId($id);
        if (!is_object($eqLogic) || $eqLogic->getEqType_name() !== 'mira') {
            throw new Exception('Mira favoris : équipement introuvable.');
        }
        // Read SAVED configuration; the current browser form is never trusted.
        $eqLogic->pushNavigationFavorites();
        ajax::success(array('ok' => true));
    }

    if (init('action') == 'GetHTTP') {
        $url = (init('url'));
        $ch = curl_init();
        curl_setopt($ch, CURLOPT_URL, $url);
        curl_setopt($ch, CURLOPT_RETURNTRANSFER, 1);
        curl_setopt($ch, CURLOPT_CONNECTTIMEOUT, 2);
        curl_setopt($ch, CURLOPT_TIMEOUT, 5);
        $result = curl_exec($ch);
        if (curl_errno($ch)) {
        }
        curl_close($ch);
        ajax::success($result);
    }

    throw new Exception(__('Aucune méthode correspondante à', __FILE__) . ' : ' . init('action'));
}
catch (Exception $e) {
    ajax::error(displayException($e), $e->getCode());
}
