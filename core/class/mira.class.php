<?php
/* This file is part of Jeedom.
*
* Jeedom is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* Jeedom is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with Jeedom. If not, see <http://www.gnu.org/licenses/>.
*/

/* * ***************************Includes********************************* */
require_once dirname(__FILE__) . '/../../../../core/php/core.inc.php';

class mira extends eqLogic {

  /**
   * Jeedom owns the menu shortcuts. This POST changes only the nav favorites;
   * it never programs widgets and cannot trigger any physical action.
   * Returns true on an authenticated acknowledgement from firmware >= 0.7.2.
   */
  public function pushNavigationFavorites() {
    $ip = trim((string)$this->getConfiguration('adresseip', ''));
    $password = (string)$this->getConfiguration('password', '');
    if ($ip === '' || $password === '') {
      throw new Exception('Mira favoris : IP ou mot de passe non renseigné.');
    }
    // The equipment already stores a plain hostname/IP, not a URL.
    if (!preg_match('/^[A-Za-z0-9.\-]+$/D', $ip)) {
      throw new Exception('Mira favoris : adresse IP ou nom d’hôte invalide.');
    }

    $defaults = array('0', '1', '2');
    $favorites = array();
    $used = array();
    for ($slot = 1; $slot <= 3; ++$slot) {
      $value = (string)$this->getConfiguration('nav_fav' . $slot, $defaults[$slot - 1]);
      if ($value === '') $value = $defaults[$slot - 1]; // existing equipments
      if (!in_array($value, array('0','1','2','3','4','255'), true)) {
        throw new Exception('Mira favoris : page inconnue sur le raccourci ' . $slot);
      }
      if ($value !== '255' && isset($used[$value])) {
        throw new Exception('Mira favoris : une page ne peut pas être choisie deux fois.');
      }
      $used[$value] = true;
      $favorites[] = (int)$value;
    }

    $payload = json_encode(array('favorites' => $favorites));
    $ch = curl_init('http://' . $ip . '/navfavorites');
    curl_setopt($ch, CURLOPT_POST, true);
    curl_setopt($ch, CURLOPT_POSTFIELDS, http_build_query(array('key' => $password, 'json' => $payload)));
    curl_setopt($ch, CURLOPT_HTTPHEADER, array('Content-Type: application/x-www-form-urlencoded'));
    curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);
    curl_setopt($ch, CURLOPT_CONNECTTIMEOUT, 2);
    curl_setopt($ch, CURLOPT_TIMEOUT, 4);
    $raw = curl_exec($ch);
    $errno = curl_errno($ch);
    $code = (int)curl_getinfo($ch, CURLINFO_HTTP_CODE);
    curl_close($ch);
    if ($errno || $code !== 200 || trim((string)$raw) !== 'OK') {
      throw new Exception('Mira favoris : écran non synchronisé (HTTP ' . $code .
        '). Vérifier le firmware 0.7.2 et utiliser « Réenvoyer les favoris ».');
    }
    log::add('mira', 'debug', '[Navigation] favoris synchronisés sur ' . $this->getName());
    return true;
  }

  /** Saved Jeedom equipment configuration is the source of truth. */
  public function postSave() {
    if (!$this->getId()) return;
    // No network error may prevent saving or modify widget commands.
    try { $this->pushNavigationFavorites(); }
    catch (Exception $e) {
      log::add('mira', 'warning', '[Navigation] ' . $this->getName() . ': ' . $e->getMessage());
    }
  }
  

  /**
   * Local GPIO state is authoritative for GPIO-linked toggles.
   * Read-only: never call On/Off actions or send an /updatenr command.
   * One HTTP read per screen, regardless of the number of relay widgets.
   */
  public function syncGpioStates() {
    $candidates = array();
    foreach ($this->getCmd('info') as $stateCmd) {
      if (!is_object($stateCmd) || $stateCmd->getConfiguration('type') !== 'toggle') continue;
      $widget = (string)$stateCmd->getLogicalId();
      $onCmd = $this->getCmd(null, 'On_' . $widget);
      $offCmd = $this->getCmd(null, 'Off_' . $widget);
      if (!is_object($onCmd) || !is_object($offCmd)) continue;
      // Two independent commands MUST address one shared GPIO at opposite levels.
      if ($onCmd->getConfiguration('mira_source','jeedom') !== 'gpio' ||
          $offCmd->getConfiguration('mira_source','jeedom') !== 'gpio') continue;
      $pin = (int)$onCmd->getConfiguration('mira_gpio');
      if (!in_array($pin, array(10,11,12,13,14,21), true) ||
          (int)$offCmd->getConfiguration('mira_gpio') !== $pin) continue;
      $onLevel = (string)$onCmd->getConfiguration('mira_level','high');
      $offLevel = (string)$offCmd->getConfiguration('mira_level','low');
      if (!in_array($onLevel,array('low','high'),true) ||
          !in_array($offLevel,array('low','high'),true) || $onLevel === $offLevel) continue;
      // An explicit state source elsewhere takes precedence over local inference.
      $stateSource = (string)$stateCmd->getConfiguration('mira_source','jeedom');
      if ($stateSource === 'jeedom' && (string)$stateCmd->getConfiguration('calcul','') !== '') continue;
      if ($stateSource === 'gpio' && (int)$stateCmd->getConfiguration('mira_gpio') !== $pin) continue;
      $candidates[$widget] = array('pin'=>$pin, 'onHigh'=>$onLevel === 'high');
    }
    if (count($candidates) === 0) return 0;

    $ip = trim((string)$this->getConfiguration('adresseip'));
    $pass = (string)$this->getConfiguration('password');
    if (!filter_var($ip,FILTER_VALIDATE_IP,FILTER_FLAG_IPV4) || $pass === '') return 0;
    $ch = curl_init('http://' . $ip . '/gpio/state');
    curl_setopt_array($ch,array(CURLOPT_RETURNTRANSFER=>true,
      CURLOPT_CONNECTTIMEOUT=>2,CURLOPT_TIMEOUT=>4,
      CURLOPT_HTTPAUTH=>CURLAUTH_BASIC,CURLOPT_USERPWD=>'admin:'.$pass));
    $response = curl_exec($ch);
    $code = (int)curl_getinfo($ch,CURLINFO_HTTP_CODE);
    curl_close($ch);
    if ($response === false || $code !== 200) {
      log::add('mira','debug','[GPIO] Retour d etat inaccessible sur '.$this->getName().' (HTTP '.$code.')');
      return 0;
    }
    $data = json_decode($response,true);
    if (!is_array($data) || !isset($data['pins']) || !is_array($data['pins'])) return 0;
    $pins = array();
    foreach ($data['pins'] as $p) {
      if (!is_array($p) || !isset($p['gpio']) || !isset($p['mode']) ||
          !array_key_exists('state',$p) || !array_key_exists('inverted',$p)) continue;
      if ($p['mode'] !== 'output') continue;
      $pins[(int)$p['gpio']] = ((bool)$p['state'] !== (bool)$p['inverted']);
    }
    $changed = 0;
    foreach ($candidates as $widget=>$spec) {
      if (!array_key_exists($spec['pin'],$pins)) continue;
      $value = $pins[$spec['pin']] === $spec['onHigh'] ? '1' : '0';
      // Jeedom updates only the Info command cache/event, without moving any relay.
      $this->checkAndUpdateCmd($widget,$value);
      ++$changed;
    }
    return $changed;
  }

  // Jeedom's once-a-minute scheduler. A direct Jeedom action still updates immediately.
  public static function cron() {
    foreach (eqLogic::byType('mira',true) as $eqLogic) {
      try { $eqLogic->syncGpioStates(); }
      catch (Throwable $e) {
        log::add('mira','warning','[GPIO] Lecture état : '.$e->getMessage());
      }
    }
  }

  public static function cron15() {
  	foreach (eqLogic::byType('mira', true) as $eqLogic) {
      if ($eqLogic->getConfiguration('ping') == "1"){
        try {
          $exec_string = 'sudo ping -c 1 -w 1 ' . $eqLogic->getConfiguration('adresseip') ;
          exec($exec_string, $output, $return);
          $output = array_values(array_filter($output));
          $latency = "-1";
          if (!empty($output[1])) {
            if (count($output) >= 5) {				
              $response = preg_match("/time(?:=|<)(?<time>[\.0-9]+)(?:|\s)ms/", $output[count($output)-4], $matches);
                  if ($response > 0 && isset($matches['time'])) {
                      $latency = $matches['time'];
                  }				
              }	
          }

          if ($latency == -1){
            log::add(__CLASS__, 'debug', $eqLogic->getHumanName(). ", " .$eqLogic->getConfiguration('adresseip'). ": Ping NOK");
            $name = "Statut";
    		$info = $eqLogic->getCmd(null, $name);
    		$eqLogic = $info->getEqLogic();
            $eqLogic->checkAndUpdateCmd($name, "0");   
          }else{          
            log::add(__CLASS__, 'debug', $eqLogic->getHumanName(). ", " .$eqLogic->getConfiguration('adresseip'). ": Ping OK, Latance: ".$latency);
            $eqLogic->refresh();            
          }

        } catch (Exception $exc) {
          log::add(__CLASS__, 'error', "Expression cron non valide pour ping: ". $eqLogic->getHumanName());					
        }
      }      
	}
  }
  public static function event() {
		
	if (init('value', init('v')) != 'json') {
      	if (init('id') != '') {
			$cmd = miraCmd::byId(init('id'));
			if (!is_object($cmd) || $cmd->getEqType() != 'mira') {
				throw new Exception(__('Commande ID mira inconnu ou la commande n\'est pas de type mira', __FILE__) . ' : ' . init('id'));
			}          	
		} 
		if (!is_object($cmd)) {
			throw new Exception(__('Commande introuvable', __FILE__) . ' : ' . json_encode($_GET));
		}
        // Firmware GPIO DEV4: a relay has already changed locally.
        // The callback must READ its state; it must NEVER execute refresh,
        // On/Off, or any Jeedom command that could actuate something again.
        if ((string)init('gpio_sync', '') === '1') {
          if ($cmd->getLogicalId() !== 'refresh') {
            throw new Exception('GPIO sync: commande non autorisée');
          }
          $eqLogic = $cmd->getEqLogic();
          if (!is_object($eqLogic)) throw new Exception('GPIO sync: écran inconnu');
          $eqLogic->syncGpioStates();
          echo init('id');
          return;
        }

		$type = $cmd->getType();
    	if($type == 'action'){
          log::add('mira', 'debug', $cmd->getName()." ACTION HTTP");
              	
          $cmd->execute();
          echo init('id');
        
        }else{
          log::add('mira', 'debug', $cmd->getName()." INFO HTTP");
          $cmd->event(init('value', init('v')));
          $eqLogic = $cmd->getEqLogic();
          $type = $cmd->getConfiguration('type');
          
          if ($type == 'info'){
            //$info = $eqLogic->getCmd(null, $name);
            $fils = $cmd->getConfiguration('calcul');
            if ($fils != ''){
              $test = cmd::byString($fils);
              $cmd::byString($fils)->event(init('value', init('v')));              
            }
          }else{
            $name = $cmd->getConfiguration('fils');
            $info = $eqLogic->getCmd(null, $name);
            $id = $info->getId();
            if ($type == "list" || $info->getConfiguration('calcul') != ''){
              $result = init('value', init('v'));
              if ($type == "list"){
                  /* Toujours laisser la commande action(list) traiter la selection.
                   * Elle choisira soit le mapping propre a l'option, soit le
                   * comportement select historique en fallback.
                   */
                  $options_action = array('select'=>$result);
              }else if ($type == "range"){
                  $options_action = array('slider'=>$result);
              }else if ($type == "color"){
                  $options_action = array('color'=>$result);
              }
             cmd::byId($id)->execCmd($options_action, $cache=0);
            }	
          }
         echo init('id');
        }
    }else{
        if (init('id') != '') {
			$cmd = miraCmd::byId(init('id'));
			if (!is_object($cmd) || $cmd->getEqType() != 'mira') {
				throw new Exception(__('Commande ID mira inconnu ou la commande n\'est pas de type mira', __FILE__) . ' : ' . init('id'));
			}
		} 
      	$json = $cmd->getConfiguration('json');
      	echo $json;
    }
  }
  
  
  
  public function preUpdate() {  
    $retour_module = $this->GetModule();    
    if ($retour_module != "Erreur" && $retour_module != ""){          
      $this->setConfiguration('module',$retour_module);
    }else{
      throw new Exception(__("Oups! N'y aurait-il pas un petit problème dans la saisie des informations?", __FILE__));
      die();
    }
  }
  
  public function postUpdate() {
    $retour_module = $this->GetModule();    
    if ($retour_module != "Erreur" && $retour_module != ""){          
      $this->setConfiguration('module',$retour_module);
    }else{
      throw new Exception(__("Oups! N'y aurait-il pas un petit problème dans la saisie des informations?", __FILE__));
      die();
    }
    
    if ($this->getConfiguration('adresseip') == '') {
      throw new Exception(__("L'adresse IP ne peut pas être vide", __FILE__));
    }else{
      	if ($this->getConfiguration('password') == '') {
      		throw new Exception(__("Le mot de passe ne peut pas être vide", __FILE__));
    	}else{
          	
          	$json_index = $this->GetIndex();
              
            if ($json_index == 'Erreur'){
                throw new Exception(__("Oups! N'y aurait-il pas un petit problème dans la saisie des informations?", __FILE__));
                die();
            }
          
          	$json_count = count($json_index);
          	if ($json_count == 0){
                /* Un index vide est valide : l'utilisateur peut vouloir supprimer
                 * tous les widgets. GetIndex() a deja distingue ce cas d'une erreur
                 * de communication via la valeur "Erreur". */
                log::add(__CLASS__, 'debug', '[SyncCmd] index ecran vide : suppression de tous les widgets Jeedom');
            }
          
          	$ordre = 0;
          log::add(__CLASS__, 'debug', "DEBUT");
              
          
          	$refresh = $this->getCmd(null, 'refresh');
            if (!is_object($refresh)) {
                $refresh = new miraCmd();
                $refresh->setName(__('Rafraichir', __FILE__));
              	$refresh->setEqLogic_id($this->getId());
                $refresh->setLogicalId('refresh');
                $refresh->setType('action');
                $refresh->setSubType('other');
              	$refresh->setConfiguration('type','refresh');
                $refresh->setIsVisible(1);
                $refresh->setConfiguration('visible_cmd',"1");
                $refresh->setConfiguration('commande',"-1");
            }
            $refresh->setOrder($ordre);
          	$ordre++;
            $refresh->save();
          
            $name = "Init cmd";            
            $init_cmd = $this->getCmd(null, 'init_cmd');
            if (!is_object($init_cmd)) {
              log::add(__CLASS__, 'debug', "Ajout du bouton d'initialisation");
              $init_cmd = new miraCmd();
              $init_cmd->setName(__($name, __FILE__));
              $init_cmd->setLogicalId('init_cmd');                
              $init_cmd->setEqLogic_id($this->getId());
              if($this->getConfiguration('widgets') == '1') $init_cmd->setTemplate('dashboard', 'mira_button');            
              if($this->getConfiguration('widgets') == '1') $init_cmd->setTemplate('mobile', 'mira_button');            
              $init_cmd->setType('action');
              $init_cmd->setSubType('other');
              $init_cmd->setConfiguration('type','init cmd');
              $init_cmd->setIsVisible(0);
              $init_cmd->setConfiguration('visible_cmd',"0");
              $init_cmd->setConfiguration('commande',"-1");
            }
          	$init_cmd->setOrder($ordre);
          	$ordre++;
            $init_cmd->save();
          
          	$name = "Redémarrer";            
            $info = $this->getCmd(null, $name);
            if (!is_object($info)) {
              log::add(__CLASS__, 'debug', "Ajout du bouton de redémarrage");
              $info = new miraCmd();
              $info->setName(__($name, __FILE__));
              $info->setLogicalId($name);
              $info->setEqLogic_id($this->getId());
              if($this->getConfiguration('widgets') == '1') $info->setTemplate('dashboard', 'mira_button');            
              if($this->getConfiguration('widgets') == '1') $info->setTemplate('mobile', 'mira_button');            
              $info->setType('action');
              $info->setSubType('other');
              $info->setConfiguration('type','reboot');
              $info->setIsVisible(0);
              $info->setConfiguration('visible_cmd',"1");
              $info->setConfiguration('commande',"-1");
            }
          	$info->setOrder($ordre);
          	$ordre++;
            $info->save();
          
          	$name = "Statut";            
            $info = $this->getCmd(null, $name);
            if (!is_object($info)) {
              log::add(__CLASS__, 'debug', "Ajout du statut");
              $info = new miraCmd();
              $info->setName(__($name, __FILE__));
              $info->setLogicalId($name);
              $info->setEqLogic_id($this->getId());
              $info->setTemplate('dashboard', 'mira_status');
        	  $info->setTemplate('mobile', 'mira_status');
        	  $info->setType('info');
              $info->setSubType('binary');              
              $info->setConfiguration('type','status');
              $info->setIsVisible(1);
              $info->setConfiguration('visible_cmd',"1");
              $info->setConfiguration('commande',"-1");                
            }            
            $info->setOrder($ordre);
          	$ordre++;
          	$info->save();
          
          	//Création des commandes
			for ($i = 0; $i < $json_count; $i++) {
              
                if ($json_index[$i]['type'] == 'info'){					
                    $cmd_id = $json_index[$i]['id'];
                    $name = $json_index[$i]['nom'];
                    $value = $json_index[$i]['value'];
                    $unite = $json_index[$i]['unit'];
                  	log::add(__CLASS__, 'debug', "Ajout d'une info: " . $name);
                  
                    $info = $this->getCmd(null, $name);
                  	if (!is_object($info)) {
                        $info = new miraCmd();
                        $info->setName(__($name, __FILE__));
                      	$info->setLogicalId($name);
                        $info->setEqLogic_id($this->getId());
                        if($this->getConfiguration('widgets') == '1') $info->setTemplate('dashboard', 'core::tile');
                        if($this->getConfiguration('widgets') == '1') $info->setTemplate('mobile', 'core::tile');
                        $info->setType('info');
                        $info->setSubType('string');
                        $info->setConfiguration('type','info');                        
                        $info->setConfiguration('visible_cmd',"1");
                    }
                  
                  	$info->setOrder($ordre);
          			$ordre++;
          			$info->setConfiguration('commande',$cmd_id);
                    $info->setUnite($unite);                  
                    $info->save();
                  	
                }else if ($json_index[$i]['type'] == 'toggle'){
                    $name = $json_index[$i]['nom'];          
                    $state = $json_index[$i]['state'];
                    $id_0 = $json_index[$i]['id0'];
                    $id_1 = $json_index[$i]['id1'];
                  	log::add(__CLASS__, 'debug', "Ajout d'un toggle: " . $name);
					
                    $info = $this->getCmd(null, $name);
                    if (!is_object($info)) {
                        $info = new miraCmd();
                        $info->setName(__($name, __FILE__));
                      	$info->setLogicalId($name);
                        $info->setEqLogic_id($this->getId());
                        $info->setType('info');
                        $info->setConfiguration('type','toggle');
                        $info->setConfiguration('visible_cmd',"1");                      	
                        $info->setSubType('binary');
                        $info->setIsVisible(0);
                    }                    
                    $info->setOrder($ordre);
          			$ordre++;
                  	$info->setConfiguration('state',$state);
          			$info->setConfiguration('commande',$id_0);
                    $info->setConfiguration('id_0',$id_0);
                    $info->setConfiguration('id_1',$id_1);
                    $info->save();
					
                  
                    $info_id = $info->getId();
                    $name_0 = "Off_" . $name;
                    $cmd_0 = $this->getCmd(null, $name_0);
                    if (!is_object($cmd_0)) {
                        $cmd_0 = new miraCmd();
                        $cmd_0->setName(__($name_0, __FILE__));
                      	$cmd_0->setLogicalId($name_0);
                        $cmd_0->setEqLogic_id($this->getId());
                        if($this->getConfiguration('widgets') == '1') $cmd_0->setTemplate('dashboard', 'mira_toggle');
                        if($this->getConfiguration('widgets') == '1') $cmd_0->setTemplate('mobile', 'mira_toggle');
                        $cmd_0->setType('action');
                        $cmd_0->setSubType('other');
                        $cmd_0->setConfiguration('type','toggle');
                        $cmd_0->setConfiguration('state','Off');                        
                        $cmd_0->setConfiguration('visible_cmd',"1");
                    } 
                  	$cmd_0->setOrder($ordre);
          			$ordre++;
          			$cmd_0->setvalue($info_id);          
                    $cmd_0->setConfiguration('parent',$name);
                    $cmd_0->setConfiguration('commande',$id_0);
                    $cmd_0->save();

                    $name_1 = "On_".$name;
                    $cmd_1 = $this->getCmd(null, $name_1);
                    if (!is_object($cmd_1)) {
                        $cmd_1 = new miraCmd();
                        $cmd_1->setName(__($name_1, __FILE__));
                      	$cmd_1->setLogicalId($name_1);
                        $cmd_1->setEqLogic_id($this->getId());
                        if($this->getConfiguration('widgets') == '1') $cmd_1->setTemplate('dashboard', 'mira_toggle');
                        if($this->getConfiguration('widgets') == '1') $cmd_1->setTemplate('mobile', 'mira_toggle');
                        $cmd_1->setType('action');
                        $cmd_1->setSubType('other');
                        $cmd_1->setConfiguration('type','toggle');
                        $cmd_1->setConfiguration('state','On');  
                        $cmd_1->setConfiguration('visible_cmd',"1");
                    }                    
                    $cmd_1->setOrder($ordre);
          			$ordre++;
          			$cmd_1->setvalue($info_id);          
                    $cmd_1->setConfiguration('parent',$name);
                    $cmd_1->setConfiguration('commande',$id_1);                        		 
                    $cmd_1->save();
                  
                  	$info->setConfiguration('fils0',$name_0);
                  	$info->setConfiguration('fils1',$name_1);
                    $info->save();
					
                }else if ($json_index[$i]['type'] == 'range'){
                  	$id_cmd = $json_index[$i]['id'];
                    $name = $json_index[$i]['nom'];
                    $value = $json_index[$i]['value'];
                    $min = $json_index[$i]['min'];
                    $max = $json_index[$i]['max'];
                    log::add(__CLASS__, 'debug', "Ajout d'un curseur: " . $name);
                  
                  	$info = $this->getCmd(null, 'Value_'.$name);
                    if (!is_object($info)) {
                        $info = new miraCmd();
                        $info->setName(__('Value_'.$name, __FILE__));
                      	$info->setLogicalId('Value_'.$name);
                        $info->setEqLogic_id($this->getId());
                        $info->setType('info');
                        $info->setConfiguration('type','range');
                        $info->setSubType('numeric');                        
                        $info->setIsVisible(0);
                        $info->setConfiguration('visible_cmd',"1");
                    } 
                    $info->setOrder($ordre);
          			$ordre++;
          			$info->setConfiguration('value',$value);
                    $info->setConfiguration('commande',$id_cmd);
                    $info->setConfiguration('minValue', $min);
                    $info->setConfiguration('maxValue', $max);
                    $info->save();
                                     	
                    $info_id = $info->getId();                  	
                    $cmd = $this->getCmd(null, $name);
                  	if (!is_object($cmd)) {
                        $cmd = new miraCmd();
                        $cmd->setName(__($name, __FILE__));
                      	$cmd->setLogicalId($name);
                        $cmd->setEqLogic_id($this->getId());
                        if($this->getConfiguration('widgets') == '1') $cmd->setTemplate('dashboard', 'mira_slider');
                        if($this->getConfiguration('widgets') == '1') $cmd->setTemplate('mobile', 'mira_slider');
                        $cmd->setType('action');
                        $cmd->setSubType('slider');
                        $cmd->setConfiguration('type','range');
                        $cmd->setConfiguration('visible_cmd',"1");
                    }  
                    $cmd->setOrder($ordre);
          			$ordre++;
          			$cmd->setConfiguration('minValue', $min);
                    $cmd->setConfiguration('maxValue', $max);
                    $cmd->setConfiguration('commande',$id_cmd);
                    $cmd->setConfiguration('parent','Value_'.$name);
                    
                    $cmd->setValue($info_id);                        
                    $cmd->save(); 
                  
                  	$info->setConfiguration('fils',$name);
                  	$info->save();
                  	log::add(__CLASS__, 'debug', "Finish" . $name);
                  
                  
                }else if ($json_index[$i]['type'] == 'list'){
                  
                  	$id_cmd = $json_index[$i]['id'];
                    $name = $json_index[$i]['nom'];
                    $namelist = $json_index[$i]['nomliste'];
                    $value = $json_index[$i]['value'];
                  	log::add(__CLASS__, 'debug', "Ajout d'un item de liste " . $name);
                  
                  	$info = $this->getCmd(null, 'Value_'.$namelist);
                    if (!is_object($info)) {
                        $info = new miraCmd();
                        $info->setName(__('Value_'.$namelist, __FILE__));
                        $info->setLogicalId('Value_'.$namelist);
                        $info->setEqLogic_id($this->getId());
                        $info->setType('info');
                        $info->setConfiguration('type','list');
                        $info->setSubType('string');
                        $info->setIsVisible(0);
                        $info->setConfiguration('visible_cmd',"1");
                    }                    
                    $info->setOrder($ordre);
          			$ordre++;
          			$info->setConfiguration('commande',$id_cmd);
                    $info->setConfiguration('listname',$namelist); 
                    $info->save();
                                           
                  	$info_id = $info->getId();                  	
                    $cmd = $this->getCmd(null, $namelist);
                  	if (!is_object($cmd)) {
                        $cmd = new miraCmd();
                        $cmd->setName(__($namelist, __FILE__));
                        $cmd->setLogicalId($namelist);
                        $cmd->setEqLogic_id($this->getId());
                        $cmd->setType('action');
                        $cmd->setSubType('select');
                        $cmd->setConfiguration('type','list');
                        $cmd->setConfiguration('visible_cmd',"1");
                    }
					
                  	$work_list = "";
                  	for ($j = 0; $j < $json_count; $j++) {
                      if ($json_index[$j]['type'] == 'list'){                        
                        if($namelist == $json_index[$j]['nomliste']){
                          if ($work_list.$json_index[$j]['nom'] != ""){
                          	$work_list = $work_list.$json_index[$j]['id'].'|'.$json_index[$j]['nom'].';';
                          }
                        }                        
                      }
                  	}
                  	$cmd->setOrder($ordre);
          			$ordre++;
          			$final_list = substr($work_list, 0, -1);                    
                    $cmd->setConfiguration('listValue', $final_list);
                    $cmd->setConfiguration('commande',$id_cmd);
                  	$cmd->setConfiguration('list',$namelist);
                    $cmd->setConfiguration('parent','Value_'.$namelist);
                    $cmd->setConfiguration('state',$name);
                    $cmd->setValue($info_id);  
                    $cmd->save();
                  
                  	$info->setConfiguration('fils',$namelist);
                  	$info->save();
                  
                  
                }else if ($json_index[$i]['type'] == 'button' && preg_match('/^Multi_(Clic|Double|Triple|Long)_(.+)$/u', (string)$json_index[$i]['nom'], $parts)){
                    $gesture=$parts[1];
                    $widget=$parts[2];
                    $name=(string)$json_index[$i]['nom'];
                    $masterName='Multi_'.$widget;
                    if ($gesture==='Clic') {
                        $master=$this->getCmd(null,$masterName);
                        if (!is_object($master)) {
                            $master=new miraCmd();
                            $master->setEqLogic_id($this->getId());
                            $master->setLogicalId($masterName);
                            $master->setName($widget.' · Multi-actions');
                            $master->setType('action');
                            $master->setSubType('other');
                            $master->setIsVisible(0);
                            $master->setConfiguration('type','multi_master');
                            $master->setConfiguration('visible_cmd','1');
                        }
                        $master->setOrder($ordre++);
                        $master->setConfiguration('type','multi_master');
                        if (!$master->getConfiguration('multi_trigger'))
                            $master->setConfiguration('multi_trigger','gpio');
                        $master->save();
                    }
                    $button=$this->getCmd(null,$name);
                    if (!is_object($button)) {
                        $button=new miraCmd();
                        $button->setEqLogic_id($this->getId());
                        $button->setLogicalId($name);
                        $button->setName($widget.' · '.array(
                          'Clic'=>'Clic simple','Double'=>'Double clic',
                          'Triple'=>'Triple clic','Long'=>'Appui long')[$gesture]);
                        $button->setType('action');
                        $button->setSubType('other');
                        $button->setIsVisible(0);
                        $button->setConfiguration('visible_cmd','1');
                        $button->setConfiguration('mira_source','jeedom');
                    }
                    $button->setOrder($ordre++);
                    $button->setConfiguration('type','multi_action');
                    if (!$button->getConfiguration('multi_mode'))
                        $button->setConfiguration('multi_mode','scenario');
                    $button->setConfiguration('commande',$json_index[$i]['id']);
                    $button->setConfiguration('multi_parent',$masterName);
                    $button->save();

                }else if ($json_index[$i]['type'] == 'button'){
                  	$id_cmd = $json_index[$i]['id'];
                    $name = $json_index[$i]['nom'];                    
                  	log::add(__CLASS__, 'debug', "Ajout d'un bouton " . $name); 
					
                  	$info = $this->getCmd(null, $name);
                    if (!is_object($info)) {
                        $info = new miraCmd();
                        $info->setName(__($name, __FILE__));
                      	$info->setLogicalId($name);
                        $info->setEqLogic_id($this->getId());
                        if($this->getConfiguration('widgets') == '1') $info->setTemplate('dashboard', 'mira_button');
                        if($this->getConfiguration('widgets') == '1') $info->setTemplate('mobile', 'mira_button');
                        $info->setType('action');
                        $info->setSubType('other');
                        $info->setConfiguration('type','button');
                    	$info->setConfiguration('visible_cmd',"1");
                    }                    
                    $info->setOrder($ordre);
          			$ordre++;
          			$info->setConfiguration('commande',$id_cmd);                  	
                    $info->save();
                  
                }else if ($json_index[$i]['type'] == 'color'){
                  	
                  	$id_cmd = $json_index[$i]['id'];
                    $name = $json_index[$i]['nom'];
                    $color_r = $json_index[$i]['r'];
                    $color_g = $json_index[$i]['g'];
                    $color_b = $json_index[$i]['b'];
                  	$hex_value= sprintf("#%02x%02x%02x", $color_r, $color_g, $color_b);
					log::add(__CLASS__, 'debug', "Ajout d'une couleur " . $name);                  	
                  
                  	$info = $this->getCmd(null, 'Value_'.$name);
                    if (!is_object($info)) {
                        $info = new miraCmd();
                        $info->setName(__('Value_'.$name, __FILE__));
                        $info->setLogicalId('Value_'.$name);
                        $info->setEqLogic_id($this->getId());
                        $info->setConfiguration('type','color');
                        $info->setType('info');
                        $info->setSubType('string');
                        $info->setIsVisible(0);
                      	$info->setConfiguration('visible_cmd',"1");
                    }
                    
                    $info->setOrder($ordre);
          			$ordre++;          			
                    $info->setConfiguration('commande',$id_cmd);
                    $info->save();
                                    
                  	$info_id = $info->getId();                  
                  	$cmd = $this->getCmd(null, $name);
                    if (!is_object($cmd)) {
                        $cmd = new miraCmd();
                        $cmd->setName(__($name, __FILE__));
                      	$cmd->setLogicalId($name);
                        $cmd->setEqLogic_id($this->getId());
                        if($this->getConfiguration('widgets') == '1') $cmd->setTemplate('dashboard', 'mira_color');
                        if($this->getConfiguration('widgets') == '1') $cmd->setTemplate('mobile', 'mira_color');
                        $cmd->setType('action');
                        $cmd->setSubType('color');
                      	$cmd->setConfiguration('type','color');
                      	$cmd->setConfiguration('parameters', 'color=FFFFFF');
                    	$cmd->setConfiguration('visible_cmd',"1");
                    }
                    
                    $cmd->setOrder($ordre);
          			$ordre++;
          			$cmd->setConfiguration('commande',$id_cmd);
                  	$cmd->setConfiguration('parent','Value_'.$name);
                    $cmd->setConfiguration('state',$hex_value);
                    $cmd->setValue($info_id);                  	
                    $cmd->save();
                  
                  	$info->setConfiguration('fils',$name);
                  	$info->save();
                  
                }
            }

            /* Synchronisation destructive des commandes Jeedom avec l'index de
             * l'ecran. Jusqu'ici postUpdate() ne faisait qu'ajouter/mettre a jour :
             * supprimer un widget sur Mira Panel laissait donc ses anciennes
             * commandes dans Jeedom.
             *
             * On reconstruit la liste exacte des logicalId attendus a partir du
             * nouvel /getindex, puis on retire uniquement les commandes de widgets
             * gerees par Mira qui n'existent plus. Les commandes systeme
             * (refresh, init_cmd, Redemarrer, Statut) ne sont jamais concernees.
             */
            $expectedLogicalIds = array(
                'refresh' => true,
                'init_cmd' => true,
                'Redémarrer' => true,
                'Statut' => true
            );

            for ($i = 0; $i < $json_count; $i++) {
                if (!isset($json_index[$i]['type'])) continue;
                $widgetType = (string)$json_index[$i]['type'];

                if ($widgetType == 'button' && preg_match('/^Multi_(Clic|Double|Triple|Long)_(.+)$/u',(string)($json_index[$i]['nom'] ?? ''),$parts)) {
                    $expectedLogicalIds[(string)$json_index[$i]['nom']]=true;
                    $expectedLogicalIds['Multi_'.$parts[2]]=true;
                } else if ($widgetType == 'info' || $widgetType == 'button') {
                    if (isset($json_index[$i]['nom'])) {
                        $expectedLogicalIds[(string)$json_index[$i]['nom']] = true;
                    }
                } else if ($widgetType == 'toggle') {
                    if (isset($json_index[$i]['nom'])) {
                        $widgetName = (string)$json_index[$i]['nom'];
                        $expectedLogicalIds[$widgetName] = true;
                        $expectedLogicalIds['Off_'.$widgetName] = true;
                        $expectedLogicalIds['On_'.$widgetName] = true;
                    }
                } else if ($widgetType == 'range' || $widgetType == 'color') {
                    if (isset($json_index[$i]['nom'])) {
                        $widgetName = (string)$json_index[$i]['nom'];
                        $expectedLogicalIds['Value_'.$widgetName] = true;
                        $expectedLogicalIds[$widgetName] = true;
                    }
                } else if ($widgetType == 'list') {
                    if (isset($json_index[$i]['nomliste'])) {
                        $listName = (string)$json_index[$i]['nomliste'];
                        $expectedLogicalIds['Value_'.$listName] = true;
                        $expectedLogicalIds[$listName] = true;
                    }
                }
            }

            $managedWidgetTypes = array('info', 'toggle', 'range', 'list', 'button', 'color', 'multi_master', 'multi_action');
            $removedCommands = 0;
            foreach ($this->getCmd() as $existingCmd) {
                if (!is_object($existingCmd)) continue;
                $logicalId = (string)$existingCmd->getLogicalId();
                $managedType = (string)$existingCmd->getConfiguration('type', '');

                if (in_array($managedType, $managedWidgetTypes, true)
                    && !isset($expectedLogicalIds[$logicalId])) {
                    log::add(__CLASS__, 'debug', '[SyncCmd] suppression commande obsolete: '.$logicalId);
                    $existingCmd->remove();
                    $removedCommands++;
                }
            }
            if ($removedCommands > 0) {
                log::add(__CLASS__, 'debug', '[SyncCmd] '.$removedCommands.' commande(s) obsolete(s) supprimee(s)');
            }
                
          	//Mise a jour SPIFFS mira
          	$mira_plugin = mira::GetParam('Jeedom');
            log::add(__CLASS__, 'debug', 'Toggle Jeedom: '.$mira_plugin);
          	
            $mira_adresseip = mira::GetParam('Jeedom_IP');
            log::add(__CLASS__, 'debug', 'Adresse IP paramétrée: '.$mira_adresseip);
          
          	$mira_key = mira::GetParam('Jeedom_Key');
          	log::add(__CLASS__, 'debug', 'Clé Jeedom décryptée: '.$mira_key);
          
          	$mira_key_mira = mira::GetParam('Jeedom_Key_mira');
          	log::add(__CLASS__, 'debug', 'Clé mira décryptée: '.$mira_key_mira);
          
            $mira_refresh = mira::GetParam('Jeedom_Refresh');
            log::add(__CLASS__, 'debug', 'Refresh paramétré: '.$mira_refresh);
          
            $mira_init = mira::GetParam('Jeedom_Init_Cmd');
            log::add(__CLASS__, 'debug', 'Init cmd paramétré: '.$mira_init);
          
            $adresseip = network::getNetworkAccess();
          	$cleapi = jeedom::getApiKey();
            $cleapi_mira = jeedom::getApiKey('mira');
            $idrefresh = $refresh->getId(); 
          	$idinitcmd = $init_cmd->getId(); 
          	
          	
          	$update = 0;
            if ($mira_plugin == "Null" || $mira_plugin == "Erreur" || $mira_adresseip == "Null" || $mira_adresseip == "Erreur" || $mira_key == "Null" || $mira_key == "Erreur" || $mira_key_mira == "Null" || $mira_key_mira == "Erreur" || $mira_refresh == "Null" || $mira_refresh == "Erreur" || $mira_init == "Null" || $mira_init == "Erreur"){
              
              log::add(__CLASS__, 'debug', 'Création des paramètres SPIFFS');
              if ($mira_plugin == "Null" || $mira_plugin == "Erreur" ){
              	$result = mira::AddListParamBool('Jeedom',"1");
              	log::add(__CLASS__, 'debug', 'Ajout du paramètre Jeedom: 1 : '.$result);
              }

              if ($mira_adresseip == "Null" || $mira_adresseip == "Erreur" ){
                $result = mira::AddListParam('Jeedom_IP',$adresseip);
                log::add(__CLASS__, 'debug', 'Ajout du paramètre Jeedom_IP: '.$adresseip.' : '.$result);
              }

              if ($mira_key == "Null" || $mira_key == "Erreur"){
                $result = mira::AddListParamCrypted('Jeedom_Key',$cleapi);
                log::add(__CLASS__, 'debug', 'Ajout du paramètre Jeedom_Key crypté: '.$cleapi.' : '.$result);
              }
              
              if ($mira_key_mira == "Null" || $mira_key == "Erreur"){
                $result = mira::AddListParamCrypted('Jeedom_Key_mira',$cleapi_mira);
                log::add(__CLASS__, 'debug', 'Ajout du paramètre Jeedom_Key_mira crypté: '.$cleapi_mira.' : '.$result);
              }

              if ($mira_refresh == "Null" || $mira_refresh == "Erreur"){
                $result = mira::AddListParam('Jeedom_Refresh',$idrefresh);
                log::add(__CLASS__, 'debug', 'Ajout du paramètre Jeedom_Refresh: '.$idrefresh.' : '.$result);
              }
              
              if ($mira_init == "Null" || $mira_init == "Erreur"){
                $result = mira::AddListParam('Jeedom_Init_Cmd',$idinitcmd);
                log::add(__CLASS__, 'debug', 'Ajout du paramètre Jeedom_Init_Cmd: '.$idinitcmd.' : '.$result);
              }
              
              mira::CreateListParam();
              
            }else{
              if ($mira_plugin != '1'){
                $result = mira::UpdateListParam('Jeedom',"1");
                log::add(__CLASS__, 'debug', 'Modification du paramètre Jeedom: 1 : '.$result);
              }
              if ($adresseip != $mira_adresseip){				
                $update++;
                $result = mira::UpdateListParam('Jeedom_IP',$adresseip);
                log::add(__CLASS__, 'debug', 'Modification du paramètre Jeedom_IP: '.$adresseip.' : '.$result);				              	
              }   
              if ($cleapi != $mira_key){
                $update++;
                $result = mira::UpdateListParam('Jeedom_Key',$cleapi);
                log::add(__CLASS__, 'debug', 'Modification du paramètre Jeedom_Key crypté: '.$cleapi.' : '.$result);
              }
              if ($cleapi_mira != $mira_key_mira){
                $update++;
                $result = mira::UpdateListParam('Jeedom_Key_mira',$cleapi_mira);
                log::add(__CLASS__, 'debug', 'Modification du paramètre Jeedom_Key_mira crypté: '.$cleapi_mira.' : '.$result);
              }
              if ($idrefresh != $mira_refresh){				
                $update++;
                $result = mira::UpdateListParam('Jeedom_Refresh',$idrefresh);
                log::add(__CLASS__, 'debug', 'Modification du paramètre Jeedom_Refresh: '.$idrefresh.' : '.$result);							
              }
              
              if ($idinitcmd != $mira_init){				
                $update++;
                $result = mira::UpdateListParam('Jeedom_Init_Cmd',$idinitcmd);
                log::add(__CLASS__, 'debug', 'Modification du paramètre Jeedom_Init_Cmd: '.$idinitcmd.' : '.$result);							
              }
              
              if ($update>0) mira::SaveListParam();

            }
          	$this->Init_mira_Module();
          	
          log::add(__CLASS__, 'debug', 'Sauvegarde OK');	
        }
    }
  }

    public function Init_mira_Module() {
    
    $json_index = $this->GetIndex(true);
    //log::add('mira', 'debug', json_encode($json_index));
    
    $name = "Statut";
    $info = $this->getCmd(null, $name);
    $eqLogic = $info->getEqLogic();
    log::add(__CLASS__, 'debug', 'Refresh module: '.$eqLogic->getHumanName()." : ".$eqLogic->getConfiguration('adresseip'));    
    
    if ($json_index == 'Erreur'){
      $eqLogic->checkAndUpdateCmd($name, "0");
      // Ne jamais tuer eqLogic.ajax.php depuis postUpdate(). Une exception sera
      // sérialisée proprement par Jeedom au lieu de produire une réponse vide.
      throw new Exception(__("Impossible de lire l'index de l'écran Mira", __FILE__));
    }else{
      $eqLogic->checkAndUpdateCmd($name, "1");
    }

    $json_count = count($json_index['actions']);
    // Un écran Mira neuf/non programmé renvoie {"actions":[]} : c'est valide.
    // Ne jamais faire die() ici : cette méthode est appelée depuis postUpdate(),
    // sinon Jeedom reçoit une réponse AJAX vide lors de la sauvegarde.
    $eqLogic->checkAndUpdateCmd($name, "1");

    if ($json_count == 0) {
      log::add(__CLASS__, 'debug', '[Init_mira_Module] écran joignable, aucun widget programmé');
      $initCmd = $this->getCmd(null, 'init_cmd');
      if (is_object($initCmd)) {
        $initCmd->setConfiguration('json', json_encode($json_index));
        $initCmd->save();
        $this->SendJeedomIDOrder($initCmd->getId());
      }
      return true;
    }

    log::add(__CLASS__, 'debug', $json_count);	
    for ($i = 0; $i < $json_count; $i++) {
      if ($json_index['actions'][$i]['type'] == 'toggle'){					
          $id = $json_index['actions'][$i]['id0'];
          $name = $json_index['actions'][$i]['nom'];                    
          $info_on = $this->getCmd(null, 'On_'.$name);
          $info_off = $this->getCmd(null, 'Off_'.$name);
          $eqLogic_on = $info_on->getId();        	
          $eqLogic_off = $info_off->getId();
          $json_index['actions'][$i]['jeedom0'] = $eqLogic_off;
          $json_index['actions'][$i]['jeedom1'] = $eqLogic_on;
          log::add(__CLASS__, 'debug', $id.' : '.$eqLogic_on.' '.$eqLogic_off);	
          
      }else if ($json_index['actions'][$i]['type'] == 'info' || $json_index['actions'][$i]['type'] == 'range' || $json_index['actions'][$i]['type'] == 'list' || $json_index['actions'][$i]['type'] == 'button' || $json_index['actions'][$i]['type'] == 'color'){
          $id = $json_index['actions'][$i]['id'];          
          if ($json_index['actions'][$i]['type'] == 'info' || $json_index['actions'][$i]['type'] == 'button'){
          	$name = $json_index['actions'][$i]['nom'];             
          } else if ($json_index['actions'][$i]['type'] == 'list'){
          	$name = 'Value_'.$json_index['actions'][$i]['nomliste'];            
          } else {
          	$name = 'Value_'.$json_index['actions'][$i]['nom'];            
          }
          $info = $this->getCmd(null, $name);
          $eqLogic = $info->getId();   
          $json_index['actions'][$i]['jeedom'] = $eqLogic;
          log::add(__CLASS__, 'debug', $id.' : '.$eqLogic);	
      }
      
      // MIRA 1.2.18 — IMPORTANT : synchronisation strictement non actionnante.
      // L'index /getindex contient des valeurs locales du firmware, notamment
      // des états par défaut OFF après reset. Ces valeurs ne représentent PAS
      // une demande d'action utilisateur et ne doivent JAMAIS être exécutées
      // dans Jeedom (éclairage, consignes, modes, couleurs...).
      // La phase d'initialisation ne fait ici que reconstruire le mapping IDs.
      // La resynchronisation future devra lire les états réels Jeedom et les
      // envoyer vers l'écran via les canaux d'information sans effet de bord.
    }
    $name = "init_cmd";
    $info = $this->getCmd(null, $name);
    if (is_object($info)) {
      $id = $info->getId();
      $info->setConfiguration('json',json_encode($json_index));
      $info->save();
      log::add('mira', 'debug', $id);
      $this->SendJeedomIDOrder($id);
    }
    return true;
  }
  
  public function preRemove() {
  }
  
  public function postRemove() {
  }
  
  public function decrypt() {
	$this->setConfiguration('password', utils::decrypt($this->getConfiguration('password')));
	$this->setConfiguration('adresseip', utils::decrypt($this->getConfiguration('adresseip')));
  }

  public function encrypt() {
	$this->setConfiguration('password', utils::encrypt($this->getConfiguration('password')));
	$this->setConfiguration('adresseip', utils::encrypt($this->getConfiguration('adresseip')));
  }
  
  public function Reboot_Module() {    
    $url = 'http://'.$this->getConfiguration('adresseip').'/reboot?key='.$this->getConfiguration('password');
    $ch = curl_init();
    curl_setopt($ch, CURLOPT_URL, $url);
    curl_setopt($ch, CURLOPT_RETURNTRANSFER, 1);
    curl_setopt($ch, CURLOPT_CONNECTTIMEOUT, 2);
    curl_setopt($ch, CURLOPT_TIMEOUT, 5);
    $result = curl_exec($ch);
    if (curl_errno($ch)) {
        return "Erreur";
    }
    curl_close($ch);    
    log::add(__CLASS__, 'debug', 'Redémarrage du module: ' . $result);
    return $module;
  }
  
  
  public function SendJeedomIDOrder($value) {
    $url = 'http://'.$this->getConfiguration('adresseip').'/sendjeedomorderid?key='.$this->getConfiguration('password').'&value='.$value;
    $ch = curl_init();
    curl_setopt($ch, CURLOPT_URL, $url);
    curl_setopt($ch, CURLOPT_RETURNTRANSFER, 1);
    curl_setopt($ch, CURLOPT_CONNECTTIMEOUT, 2);
    curl_setopt($ch, CURLOPT_TIMEOUT, 5);
    $result = curl_exec($ch);
    if (curl_errno($ch)) {
        return "Erreur";		
    }
    curl_close($ch);
    return $result;    
  }
  
  /*public function SetJeedomId($param,$value) {
    $url = 'http://'.$this->getConfiguration('adresseip').'/setjeedomid?key='.$this->getConfiguration('password').'&id='.$param.'&value='.$value;
    $ch = curl_init();
    curl_setopt($ch, CURLOPT_URL, $url);
    curl_setopt($ch, CURLOPT_RETURNTRANSFER, 1);
    curl_setopt($ch, CURLOPT_CONNECTTIMEOUT, 2);
    curl_setopt($ch, CURLOPT_TIMEOUT, 5);
    $result = curl_exec($ch);
    if (curl_errno($ch)) {
        return "Erreur";		
    }
    curl_close($ch);
    return $result;    
  }
  
  public function SetJeedomToggleId($param,$value0, $value1) {
    $url = 'http://'.$this->getConfiguration('adresseip').'/setjeedomtoggleid?key='.$this->getConfiguration('password').'&id='.$param.'&value0='.$value0.'&value1='.$value1;
    $ch = curl_init();
    curl_setopt($ch, CURLOPT_URL, $url);
    curl_setopt($ch, CURLOPT_RETURNTRANSFER, 1);
    curl_setopt($ch, CURLOPT_CONNECTTIMEOUT, 2);
    curl_setopt($ch, CURLOPT_TIMEOUT, 5);
    $result = curl_exec($ch);
    if (curl_errno($ch)) {
        return "Erreur";		  
    }
    curl_close($ch);
    return $result;    
  }*/
  
  
  public function UpdateListParam($param,$value) {
    $url = 'http://'.$this->getConfiguration('adresseip').'/updatelistparam?key='.$this->getConfiguration('password').'&param='.$param.'&value='.$value;
    $ch = curl_init();
    curl_setopt($ch, CURLOPT_URL, $url);
    curl_setopt($ch, CURLOPT_RETURNTRANSFER, 1);
    curl_setopt($ch, CURLOPT_CONNECTTIMEOUT, 2);
    curl_setopt($ch, CURLOPT_TIMEOUT, 5);
    $result = curl_exec($ch);
    if (curl_errno($ch)) {
        return "Erreur";		
    }
    curl_close($ch);
    return $result;    
  }
  
  
  
  public function SaveListParam() {
    $url = 'http://'.$this->getConfiguration('adresseip').'/savelistparam?key='.$this->getConfiguration('password');
    $ch = curl_init();
    curl_setopt($ch, CURLOPT_URL, $url);
    curl_setopt($ch, CURLOPT_RETURNTRANSFER, 1);
    curl_setopt($ch, CURLOPT_CONNECTTIMEOUT, 2);
    curl_setopt($ch, CURLOPT_TIMEOUT, 5);
    $result = curl_exec($ch);
    if (curl_errno($ch)) {
        return "Erreur";		
    }
    curl_close($ch);
    return $result;    
  }
  
  public function AddListParam($param,$value) {
    $url = 'http://'.$this->getConfiguration('adresseip').'/addlistparam?key='.$this->getConfiguration('password').'&param='.$param.'&value='.$value.'&type=hidden-text';
    $ch = curl_init();
    curl_setopt($ch, CURLOPT_URL, $url);
    curl_setopt($ch, CURLOPT_RETURNTRANSFER, 1);
    curl_setopt($ch, CURLOPT_CONNECTTIMEOUT, 2);
    curl_setopt($ch, CURLOPT_TIMEOUT, 5);
    $result = curl_exec($ch);
    if (curl_errno($ch)) {
        return "Erreur";
    }
    curl_close($ch);
    return $result;    
  }
  
  public function AddListParamCrypted($param,$value) {
    $url = 'http://'.$this->getConfiguration('adresseip').'/addlistparam?key='.$this->getConfiguration('password').'&param='.$param.'&value='.$value.'&type=crypted-text';
    $ch = curl_init();
    curl_setopt($ch, CURLOPT_URL, $url);
    curl_setopt($ch, CURLOPT_RETURNTRANSFER, 1);
    curl_setopt($ch, CURLOPT_CONNECTTIMEOUT, 2);
    curl_setopt($ch, CURLOPT_TIMEOUT, 5);
    $result = curl_exec($ch);
    if (curl_errno($ch)) {
        return "Erreur";
    }
    curl_close($ch);
    return $result;    
  }
  
  public function AddListParamBool($param,$value) {
    $url = 'http://'.$this->getConfiguration('adresseip').'/addlistparam?key='.$this->getConfiguration('password').'&param='.$param.'&value='.$value.'&type=toggle';
    $ch = curl_init();
    curl_setopt($ch, CURLOPT_URL, $url);
    curl_setopt($ch, CURLOPT_RETURNTRANSFER, 1);
    curl_setopt($ch, CURLOPT_CONNECTTIMEOUT, 2);
    curl_setopt($ch, CURLOPT_TIMEOUT, 5);
    $result = curl_exec($ch);
    if (curl_errno($ch)) {
        return "Erreur";
    }
    curl_close($ch);
    return $result;    
  }
  
  public function CreateListParam() {
    $url = 'http://'.$this->getConfiguration('adresseip').'/createlistparam?key='.$this->getConfiguration('password');
    $ch = curl_init();
    curl_setopt($ch, CURLOPT_URL, $url);
    curl_setopt($ch, CURLOPT_RETURNTRANSFER, 1);
    curl_setopt($ch, CURLOPT_CONNECTTIMEOUT, 2);
    curl_setopt($ch, CURLOPT_TIMEOUT, 5);
    $result = curl_exec($ch);
    if (curl_errno($ch)) {
        return "Erreur";		
    }
    curl_close($ch);
    return $result;    
  }
  
  public function GetParam($param) {
    $url = 'http://'.$this->getConfiguration('adresseip').'/getparam?key='.$this->getConfiguration('password').'&value='.$param;
    $ch = curl_init();
    curl_setopt($ch, CURLOPT_URL, $url);
    curl_setopt($ch, CURLOPT_RETURNTRANSFER, 1);
    curl_setopt($ch, CURLOPT_CONNECTTIMEOUT, 2);
    curl_setopt($ch, CURLOPT_TIMEOUT, 5);
    $result = curl_exec($ch);
    if (curl_errno($ch)) {
        return "Erreur";
    }
    curl_close($ch);
    return $result;    
  }
   
  
  public function GetModule() {    
    $url = 'http://'.$this->getConfiguration('adresseip').'/getmodule?key='.$this->getConfiguration('password');
    $ch = curl_init();
    curl_setopt($ch, CURLOPT_URL, $url);
    curl_setopt($ch, CURLOPT_RETURNTRANSFER, 1);
    curl_setopt($ch, CURLOPT_CONNECTTIMEOUT, 2);
    curl_setopt($ch, CURLOPT_TIMEOUT, 5);
    $result = curl_exec($ch);
    if (curl_errno($ch)) {
        return "Erreur";
    }
    curl_close($ch);
    return $result; 
  }
  
  public function GetIndex($brut = false) {
    $url = 'http://'.$this->getConfiguration('adresseip').'/getindex?key='.$this->getConfiguration('password');
    $ch = curl_init();
    curl_setopt($ch, CURLOPT_URL, $url);
    curl_setopt($ch, CURLOPT_RETURNTRANSFER, 1);
    curl_setopt($ch, CURLOPT_CONNECTTIMEOUT, 2);
    curl_setopt($ch, CURLOPT_TIMEOUT, 5);
    $result = curl_exec($ch);
    if (curl_errno($ch)) {
        curl_close($ch);
        return "Erreur";
    }
    curl_close($ch);

    if ($result === false || trim((string)$result) === '') return "Erreur";
    $jsonData = json_decode($result, true);
    if (!is_array($jsonData) || !isset($jsonData['actions']) || !is_array($jsonData['actions'])) {
        log::add(__CLASS__, 'warning', '[GetIndex] Réponse invalide reçue de /getindex');
        return "Erreur";
    }

    if ($brut) return $jsonData;
    return $jsonData['actions'];
  }
  
  public function AddListLvgl($type,$name,$loc,$min,$max,$value,$unit) {
    $url = 'http://'.$this->getConfiguration('adresseip').'/setjeedomid?key='.$this->getConfiguration('password').'&type='.$type.'&name='.$name.'&loc='.$loc.'&min='.$min.'&max='.$max.'&value='.$value.'&unit='.$unit;
    $ch = curl_init();
    curl_setopt($ch, CURLOPT_URL, $url);
    curl_setopt($ch, CURLOPT_RETURNTRANSFER, 1);
    curl_setopt($ch, CURLOPT_CONNECTTIMEOUT, 2);
    curl_setopt($ch, CURLOPT_TIMEOUT, 5);
    $result = curl_exec($ch);
    if (curl_errno($ch)) {
        return "Erreur";		
    }
    curl_close($ch);
    return $result;    
  }  
  
  public function refresh() {
    // Lecture seule : un rafraîchissement ne doit jamais déclencher les
    // commandes On/Off, le slider, les listes ni les couleurs de Jeedom.
    // L'index interne peut contenir l'état OFF par défaut d'un ESP redémarré.
    $json_index = $this->GetIndex();
    $status = $this->getCmd(null, 'Statut');
    if (!is_object($status)) {
      log::add(__CLASS__, 'warning', '[Refresh] commande Statut introuvable');
      return false;
    }
    if ($json_index === 'Erreur') {
      $this->checkAndUpdateCmd('Statut', '0');
      return false;
    }
    $this->checkAndUpdateCmd('Statut', '1');
    // Seules les sorties GPIO locales sont lues depuis l'ecran.
    // Les actionneurs Jeedom classiques gardent Jeedom comme source de verite.
    $this->syncGpioStates();
    return true;
  }

}



class miraCmd extends cmd { 
  
  
  public function preSave() {  
    $calcul = $this->getConfiguration('calcul');
    if ($this->getType() == 'info') {      
			$this->setValue($calcul);
	}
  }
  

  // Option locale disponible uniquement sur les boutons et interrupteurs.
  private function gpioEligible() {
    return ($this->getType() == 'action' && in_array($this->getConfiguration('type'), array('toggle','button','multi_action'), true)) ||
           ($this->getType() == 'info' && $this->getConfiguration('type') == 'toggle');
  }
  private function gpioRequest($route, $params = array(), $method = 'POST') {
    $eq = $this->getEqLogic();
    if (!is_object($eq)) return false;
    $ip = trim((string)$eq->getConfiguration('adresseip'));
    $pass = (string)$eq->getConfiguration('password');
    if (!filter_var($ip, FILTER_VALIDATE_IP, FILTER_FLAG_IPV4) || $pass === '') return false;
    $ch = curl_init('http://' . $ip . $route . ($method === 'GET' ? '?' . http_build_query($params) : ''));
    curl_setopt_array($ch, array(CURLOPT_RETURNTRANSFER => true, CURLOPT_CONNECTTIMEOUT => 2,
      CURLOPT_TIMEOUT => 4, CURLOPT_HTTPAUTH => CURLAUTH_BASIC, CURLOPT_USERPWD => 'admin:' . $pass));
    if ($method === 'POST') {
      curl_setopt($ch, CURLOPT_POST, true);
      curl_setopt($ch, CURLOPT_POSTFIELDS, http_build_query($params));
      curl_setopt($ch, CURLOPT_HTTPHEADER, array('X-Mira-GPIO: 1'));
    }
    $out = curl_exec($ch);
    $code = curl_getinfo($ch, CURLINFO_HTTP_CODE);
    curl_close($ch);
    return ($out !== false && $code === 200) ? $out : false;
  }
  public function postSave() {
    if ($this->getConfiguration('type') === 'multi_master') {
      $widget=substr((string)$this->getLogicalId(),strlen('Multi_'));
      $pin=(int)$this->getConfiguration('mira_input_gpio');
      $trigger=(string)$this->getConfiguration('multi_trigger','gpio');
      if (!in_array($trigger,array('jeedom','gpio','both'),true)) $trigger='gpio';
      $activePin=$trigger==='jeedom' ? 0 : $pin;
      // "Afficher" belongs ONLY to Jeedom dashboard, never Mira screen.
      if ($widget!=='' && ($activePin===0 || in_array($activePin,array(10,11,12,13,14,21),true))) {
        $ok=$this->gpioRequest('/gpio/multi/config',array('name'=>$widget,
          'gpio'=>$activePin,'visible'=>$trigger==='gpio' ? 0 : 1));
        if ($ok===false)
          log::add('mira','warning','[MULTI] Liaison impossible : '.$widget.' mode '.$trigger.' (WT32 DEV6 requis)');
      }
    }
    if ($this->gpioEligible() && $this->getConfiguration('type') !== 'multi_action') {
      $source = $this->getConfiguration('mira_source', 'jeedom') === 'gpio' ? 'gpio' : 'jeedom';
      $pin = (int)$this->getConfiguration('mira_gpio');
      $level = $this->getConfiguration('mira_level', 'high');
      if ($source === 'gpio' && (!in_array($pin,array(10,11,12,13,14,21),true) ||
                                ($level !== 'high' && $level !== 'low'))) {
        log::add('mira','warning','[GPIO] Configuration invalide : '.$this->getLogicalId());
      } else {
        $ok = $this->gpioRequest('/gpio/link', array('name'=>$this->getLogicalId(),
          'source'=>$source, 'gpio'=>$source === 'gpio'?$pin:0,
          'level'=>$this->getType() === 'info' ? 'read' : $level));
        if ($ok === false && $source === 'gpio') {
          log::add('mira','warning','[GPIO] Liaison impossible : '.$this->getLogicalId().' (firmware WT32 DEV3 requis)');
        }
      }
    }
    if ($this->getType() == 'info' && $this->getConfiguration('type') == 'info' &&
        $this->getConfiguration('calcul') != '') $this->event($this->execute());
  }
  
  private function listOptionNameFromValue($selected) {
    $listValue = $this->getConfiguration('listValue');
    if ($listValue == '') return '';
    $items = explode(';', $listValue);
    foreach ($items as $item) {
      if ($item == '') continue;
      $parts = explode('|', $item, 2);
      if (count($parts) != 2) continue;
      if ((string)$parts[0] === (string)$selected) return $parts[1];
    }
    return '';
  }

  private function listMappedAction($selected) {
    $optionName = $this->listOptionNameFromValue($selected);
    if ($optionName != '') {
      /* Le mapping est lie au nom de l'option, stable meme si les IDs Mira
       * changent apres ajout/reordonnancement d'autres widgets. */
      $stableKey = 'listAction_' . bin2hex($optionName);
      $mapped = $this->getConfiguration($stableKey);
      if ($mapped != '') return $mapped;
    }

    /* Compatibilite avec le premier patch qui stockait par ID. */
    $legacy = $this->getConfiguration('listAction_' . $selected);
    if ($legacy != '') return $legacy;
    return '';
  }

 public function execute($_options = array()) {
    $eqLogic = $this->getEqLogic();
	if ($this->getLogicalId() == 'refresh') {
        /* Un refresh relit aussi la structure de l'ecran afin de creer
         * les commandes ajoutees depuis la derniere programmation. */
        $eqLogic->postUpdate();
        $eqLogic->syncGpioStates();
      	return;
	}
   
   	if ($this->getLogicalId() == 'init_cmd') {
		$eqLogic->Init_mira_Module();
		return;
	}
    
    switch ($this->getType()) {
		case 'info':
        $type = $this->getConfiguration('type');
                $nom = $this->getLogicalId();
        if ($this->gpioEligible() && $this->getType() === 'info' &&
            $this->getConfiguration('mira_source', 'jeedom') === 'gpio') {
          $pin = (int)$this->getConfiguration('mira_gpio');
          $json = $this->gpioRequest('/gpio/state',array(), 'GET');
          $data = ($json !== false) ? json_decode($json,true) : null;
          if (!is_array($data) || !isset($data['pins'])) return '';
          $onAction = $eqLogic->getCmd(null,'On_'.$nom);
          if (!is_object($onAction) || $onAction->getConfiguration('mira_source', 'jeedom') !== 'gpio' ||
              (int)$onAction->getConfiguration('mira_gpio') !== $pin) return '';
          $onHigh = $onAction->getConfiguration('mira_level', 'high') === 'high';
          foreach ($data['pins'] as $item) {
            if ((int)$item['gpio'] !== $pin || $item['mode'] !== 'output') continue;
            $high = (bool)$item['state'] !== (bool)$item['inverted'];
            $result = $high === $onHigh ? '1' : '0';
            $this->event($result); // Reading the Info also refreshes Jeedom cache.
            return $result;
          }
          return '';
        }
        if ($this->getConfiguration('calcul') != ''&& $this->getConfiguration('type') == 'info'){
           
          $result = jeedom::evaluateExpression($this->getConfiguration('calcul'));           
		  $cmd = $this->getConfiguration('commande');
          $url = 'http://'. $eqLogic->getConfiguration('adresseip') . '/updatenr?key=' . $eqLogic->getConfiguration('password') . '&info=' . $cmd .'&value='.$result; 
          log::add('mira', 'debug', $eqLogic->getName().': INFO: info: ' . $nom .": ".$url);
          $req = 'curl -i -XGET "'.$url.'" ';
          $output = shell_exec($req);          	
          return $result;            
        
        }else if ($this->getConfiguration('calcul') != '' && $this->getConfiguration('type') == 'toggle'){
          $result = jeedom::evaluateExpression($this->getConfiguration('calcul'));           
		  $id_0 = $this->getConfiguration('id_0');
          $id_1 = $this->getConfiguration('id_1');          
          if ($result == 0 || $result == "off" || $result == "Off"){
            $url = 'http://'. $eqLogic->getConfiguration('adresseip') . '/updatenr?key=' . $eqLogic->getConfiguration('password') . '&toggle=' . $id_0;
          }else{
            $url = 'http://'. $eqLogic->getConfiguration('adresseip') . '/updatenr?key=' . $eqLogic->getConfiguration('password') . '&toggle=' . $id_1;           
          }
          log::add('mira', 'debug', $eqLogic->getName().': INFO: toogle: ' . $nom .": ".$url);
          $req = 'curl -i -XGET "'.$url.'" ';
          $output = shell_exec($req);          	
          return $result;
          
        }else if ($this->getConfiguration('calcul') != '' && $this->getConfiguration('type') == 'list') {
          // Jeedom can return either a Mira option ID or a readable mode name
          // (e.g. Confort, Éco, Arrêt). /updatenr?list=... expects a numeric
          // ID: a text name previously became 0 with String::toInt(), and the
          // thermostat never refreshed. The ESP already exposes /listvalue,
          // which resolves the option NAME into the correct Mira ID.
          $result = jeedom::evaluateExpression($this->getConfiguration('calcul'));
          $listName = (string)$this->getConfiguration('listname');
          $selected = trim((string)$result);
          $paramName = 'listvalue';
          $optionValue = $selected;

          // Read the current, synchronized option map (id|name;id|name).
          // IDs can change when widgets are reprogrammed; never hard-code them.
          $listAction = $eqLogic->getCmd(null, $listName);
          if (is_object($listAction)) {
            $entries = explode(';', (string)$listAction->getConfiguration('listValue'));
            $options = array();
            foreach ($entries as $entry) {
              $pair = explode('|', $entry, 2);
              if (count($pair) != 2) continue;
              $id = trim($pair[0]);
              $name = trim($pair[1]);
              if ($id !== '' && $name !== '') $options[] = array('id' => $id, 'name' => $name);
            }

            // ID supplied by a Jeedom numeric info: preserve legacy behavior.
            foreach ($options as $option) {
              if ($selected === $option['id']) {
                $paramName = 'list';
                $optionValue = $option['id'];
                break;
              }
            }
            if ($paramName !== 'list') {
              // Prefer exact option names, then accept harmless differences
              // in accents, spaces and case (e.g. Eco vs Éco, Hors-gel).
              $normalise = function ($name) {
                $name = trim((string)$name);
                if (function_exists('iconv')) {
                  $ascii = @iconv('UTF-8', 'ASCII//TRANSLIT//IGNORE', $name);
                  if ($ascii !== false) $name = $ascii;
                }
                return preg_replace('/[^a-z0-9]/', '', strtolower($name));
              };
              foreach ($options as $option) {
                if ($selected === $option['name']) {
                  $optionValue = $option['name'];
                  break;
                }
              }
              if ($optionValue === $selected) {
                $key = $normalise($selected);
                if ($key !== '') {
                  foreach ($options as $option) {
                    if ($normalise($option['name']) === $key) {
                      $optionValue = $option['name'];
                      break;
                    }
                  }
                }
              }
            }
          } elseif (preg_match('/^[0-9]+$/D', $selected)) {
            // Unsynchronized older installations may have only an ID.
            $paramName = 'list';
          }

          $url = 'http://' . $eqLogic->getConfiguration('adresseip') .
                 '/updatenr?key=' . rawurlencode((string)$eqLogic->getConfiguration('password')) .
                 '&' . $paramName . '=' . rawurlencode($listName) .
                 '&value=' . rawurlencode($optionValue);
          // Use HTTP directly; do not execute the Jeedom select/action command.
          // This is state feedback only, not a physical heating command.
          $ch = curl_init();
          curl_setopt($ch, CURLOPT_URL, $url);
          curl_setopt($ch, CURLOPT_RETURNTRANSFER, 1);
          curl_setopt($ch, CURLOPT_CONNECTTIMEOUT, 2);
          curl_setopt($ch, CURLOPT_TIMEOUT, 5);
          $output = curl_exec($ch);
          $errno = curl_errno($ch);
          curl_close($ch);
          if ($errno || $output === false || trim((string)$output) === 'Erreur' || trim((string)$output) === '') {
            log::add('mira', 'warning', $eqLogic->getName() . ': LIST STATE non synchronisé : ' .
                     $listName . ' = ' . $selected . ' (' . $paramName . ')');
            return 'Erreur';
          }
          log::add('mira', 'debug', $eqLogic->getName() . ': LIST STATE ' .
                   $listName . ' = ' . $selected . ' -> ID ' . $output);
          // Preserve Jeedom info value semantics (label remains a label,
          // not the screen-specific numeric command ID returned by /listvalue).
          return $result;

        }else if ($this->getConfiguration('calcul') != ''&& $this->getConfiguration('type') == 'range'){           
          $result = jeedom::evaluateExpression($this->getConfiguration('calcul'));           
		  $cmd = $this->getConfiguration('commande');
          $url = 'http://'. $eqLogic->getConfiguration('adresseip') . '/updatenr?key=' . $eqLogic->getConfiguration('password') . '&range=' . $cmd .'&value='.$result;              
          log::add('mira', 'debug', $eqLogic->getName().': INFO: range: ' . $nom .": ".$url);
          $req = 'curl -i -XGET "'.$url.'" ';
          $output = shell_exec($req);          	
          return $result;            
        
        }else if ($this->getConfiguration('calcul') != ''&& $this->getConfiguration('type') == 'color'){           
          $result = jeedom::evaluateExpression($this->getConfiguration('calcul'));           
		  list($r, $g, $b) = sscanf($result, "#%02x%02x%02x");
          $cmd = $this->getConfiguration('commande');
          $url = 'http://'. $eqLogic->getConfiguration('adresseip') . '/updatenr?key=' . $eqLogic->getConfiguration('password') . '&color=' . $cmd . '&r='.$r.'&g='.$g.'&b='.$b;             
          log::add('mira', 'debug', $eqLogic->getName().': INFO: color: ' . $nom .": ".$url);
          $req = 'curl -i -XGET "'.$url.'" ';
          $output = shell_exec($req);          	
          return $result;            
        }
        
        break;
		case 'action':
                $nom = $this->getLogicalId();
        if ($this->getConfiguration('type') === 'multi_master') {
          $simple=$eqLogic->getCmd(null,'Multi_Clic_'.substr((string)$nom,6));
          return is_object($simple) ? $simple->execute() : false;
        }
        if ($this->gpioEligible() && $this->getConfiguration('mira_source', 'jeedom') === 'gpio') {
          $pin = (int)$this->getConfiguration('mira_gpio');
          $level = $this->getConfiguration('mira_level', 'high');
          if (!in_array($pin,array(10,11,12,13,14,21),true) ||
              !in_array($level,array('high','low'),true)) return false;
          $ok = $this->gpioRequest('/gpio/raw', array('gpio'=>$pin,'level'=>$level));
          if ($ok === false) {
            log::add('mira','warning','[GPIO] Commande echouee : '.$nom);
            return false;
          }
          if ($this->getConfiguration('type') === 'toggle') {
            $eqLogic->checkAndUpdateCmd($this->getConfiguration('parent'),
                                       $this->getConfiguration('state') === 'On' ? 1 : 0);
          }
          return true; // GPIO local: pas de seconde commande Jeedom
        }
        if ($this->getConfiguration('type') === 'multi_action') {
          // The gesture is already detected by WT32; never send an action back.
          $mode=(string)$this->getConfiguration('multi_mode','scenario');
          if ($mode==='none') return true;
          if ($mode==='light') {
            $on=trim((string)$this->getConfiguration('multi_on'));
            $off=trim((string)$this->getConfiguration('multi_off'));
            $state=trim((string)$this->getConfiguration('multi_state'));
            if ($on==='' || $off==='' || $state==='') {
              log::add('mira','warning','[MULTI] ON/OFF/Etat incomplets : '.$nom);
              return false;
            }
            $stateCmd=cmd::byString($state);
            if (!is_object($stateCmd) || $stateCmd->getType()!=='info') {
              log::add('mira','warning','[MULTI] Etat Jeedom invalide : '.$nom);
              return false;
            }
            $value=$stateCmd->execCmd();
            if (is_bool($value)) $isOn=$value;
            else {
              $normalized=strtolower(trim((string)$value));
              if (in_array($normalized,array('1','on','true','oui','actif'),true)) $isOn=true;
              else if (in_array($normalized,array('0','off','false','non','inactif'),true)) $isOn=false;
              else {
                log::add('mira','warning','[MULTI] Etat non binaire : '.$nom);
                return false;
              }
            }
            $target=$isOn ? $off : $on;
          } else if ($mode==='scenario') {
            $target=trim((string)$this->getConfiguration('calcul'));
          } else return false;
          if ($target==='') return true;
          $mapped=cmd::byString($target);
          if (!is_object($mapped) || $mapped->getType()!=='action' || $mapped->getId()===$this->getId()) {
            log::add('mira','warning','[MULTI] Commande cible invalide : '.$nom);
            return false;
          }
          $mapped->execCmd();
          return true;
        }
        $cmd = $this->getConfiguration('commande');        	
        	$type = $this->getConfiguration('type');
			
        	if ($type == 'toggle'){
              	$state = $this->getConfiguration('state');
        		$parent = $this->getConfiguration('parent');              
                $url = 'http://'. $eqLogic->getConfiguration('adresseip') . '/updatenr?key=' . $eqLogic->getConfiguration('password') . '&toggle=' . $cmd;
                log::add('mira', 'debug', $eqLogic->getName().": ACTION: toogle: ".$nom.": " . $url);
              	$req = 'curl -i -XGET "'.$url.'" ';
                
              $output = shell_exec($req);                             
                $eqLogic->checkAndUpdateCmd($parent, $state);
              	if ($this->getConfiguration('calcul') != ''){
                	$result = jeedom::evaluateExpression($this->getConfiguration('calcul'));
                	cmd::byString($result)->execCmd(null, $cache=0);
              	}
              	log::add('mira', 'debug', $eqLogic->getName().": ACTION: retour: " . $url);
              	break;
              
            }else if ($type == 'range'){
              	$parent = $this->getConfiguration('parent');
              	$value = $this->getConfiguration('value');
              	$url = 'http://'. $eqLogic->getConfiguration('adresseip') . '/updatenr?key=' . $eqLogic->getConfiguration('password') . '&range=' . $cmd .'&value='.$_options['slider'];              
              	$url = str_replace(' ', '%20', $url);
                $req = 'curl -i -XGET "'.$url.'" ';
                $output = shell_exec($req);
                $eqLogic->checkAndUpdateCmd($parent, $_options['slider']);
              	if ($this->getConfiguration('calcul') != ''){
                	$result = jeedom::evaluateExpression($this->getConfiguration('calcul'));
                	log::add('mira', 'debug', $result);
          			$options_action = array('slider'=>$_options['slider']);
					cmd::byString($result)->execCmd($options_action, $cache=0);
              	}
              	log::add('mira', 'debug', $url);
        		break;
              
            }else if ($type == 'list'){
                $parent = $this->getConfiguration('parent'); 
                $list = $this->getConfiguration('list'); 
                $url = 'http://'. $eqLogic->getConfiguration('adresseip') . '/updatenr?key=' . $eqLogic->getConfiguration('password') . '&list=' . $list .'&value='.$_options['select'];
                $url = str_replace(' ', '%20', $url);
                $req = 'curl -i -XGET "'.$url.'" ';
                $output = shell_exec($req);
              	$eqLogic->checkAndUpdateCmd($parent,$_options['select']);
              
                /* Mapping 100 % dynamique : les options viennent de listValue
                 * (donc de ce que l'utilisateur a saisi dans Mira). Aucun nom
                 * comme Confort/Eco/etc. n'est code en dur. */
                $selected = isset($_options['select']) ? $_options['select'] : '';
                $optionName = $this->listOptionNameFromValue($selected);
                $mappedAction = $this->listMappedAction($selected);
                if ($mappedAction != '') {
                    $result = jeedom::evaluateExpression($mappedAction);
                    log::add('mira', 'debug', $eqLogic->getName().': LIST MAP '.($optionName != '' ? $optionName : $selected).' -> '.$mappedAction);
                    cmd::byString($result)->execCmd(null, $cache=0);
                } else if ($this->getConfiguration('calcul') != '') {
                    // Comportement historique : une seule commande action/select.
                    $result = jeedom::evaluateExpression($this->getConfiguration('calcul'));
                    $options_action = array('select'=>$selected);
                    cmd::byString($result)->execCmd($options_action, $cache=0);
                }
              	log::add('mira', 'debug', $url);
        		break;
              
            }else if ($type == 'button'){
              	$url = 'http://'. $eqLogic->getConfiguration('adresseip') . '/updatenr?key=' . $eqLogic->getConfiguration('password') . '&button=' . $cmd;
                $url = str_replace(' ', '%20', $url);
                $req = 'curl -i -XGET "'.$url.'" ';
                $output = shell_exec($req);
              	if ($this->getConfiguration('calcul') != ''){
                	$result = jeedom::evaluateExpression($this->getConfiguration('calcul'));
                	cmd::byString($result)->execCmd(null, $cache=0);
              	}
              	log::add('mira', 'debug', $url);
        		break;
              
            }else if ($type == 'color'){              
              	$parent = $this->getConfiguration('parent');
              	list($r, $g, $b) = sscanf($_options['color'], "#%02x%02x%02x");              	
              	$url = 'http://'. $eqLogic->getConfiguration('adresseip') . '/updatenr?key=' . $eqLogic->getConfiguration('password') . '&color=' . $cmd . '&r='.$r.'&g='.$g.'&b='.$b;
                $url = str_replace(' ', '%20', $url);
                $req = 'curl -i -XGET "'.$url.'" ';
                $output = shell_exec($req);
              	$eqLogic->checkAndUpdateCmd($parent, $_options['color']);
                if ($this->getConfiguration('calcul') != ''){
                	$result = jeedom::evaluateExpression($this->getConfiguration('calcul'));
                	$options_action = array('color'=>$_options['color']);
					cmd::byString($result)->execCmd($options_action, $cache=0);
              	}
              log::add('mira', 'debug', $url);
        		break;
              	              
            }else if ($type == 'reboot'){ 
              
              	$url = 'http://'.$eqLogic->getConfiguration('adresseip').'/reboot?key='.$eqLogic->getConfiguration('password');
                $req = 'curl -i -XGET "'.$url.'" ';
                $output = shell_exec($req);
                log::add(__CLASS__, 'debug', 'Redémarrage du module: ' . $result);
              	
              	break;           
            }
        
    }
  }      
}