<?php
if (!isConnect('admin')) {
	throw new Exception('{{401 - Accès non autorisé}}');
}
// Déclaration des variables obligatoires
$plugin = plugin::byId('mira');
sendVarToJS('eqType', $plugin->getId());
$eqLogics = eqLogic::byType($plugin->getId());
log::add('mira', 'debug', "Lancement script");
          
?>

                 
            	<div id="PopupDiv">
                  	<div class='miraright'><div class='mirabutton dangerbutton' id="mirabuttonannuler"><i style="margin-left:-1px;" class='icon kiko-cross'></i></div></div>  
  					<label  style='margin-top:10px;'class="control-label">{{Pour débuter la programmation, veuillez saisir les informations de connexion.}}</label>     
    				<div class="mirainputzone"> 
  					<div id="divmiraip" style="margin-top:2px; display:flex; line-height:40px; vertical-align: middle;"><div  style="width:30%;">Adresse IP&nbsp;:&nbsp;</div><input style="width:70%;" type="text" id="miraip" size="12" /></div>
                    <div id="divmiramdp" style="margin-top:2px; display:flex; line-height:40px; vertical-align: middle;"><div  style="width:30%;">Mot de passe&nbsp;:&nbsp;</div><input style="width:70%;" type="password" id="miramdp" size="12" /></div> 
                    </div>
                    <div id="miraboutontester" class="mirabutton" style="background-color:var(--al-warning-color); width:95%; margin-top:10px;">Tester la connexion</div> 
                    <div id="miradesignerbar" style="display:none;">
                      <div id="mirabuttondesigner" class="mirabutton mira-designer-launch"><i class="fas fa-object-group"></i> {{Designer visuel}}</div>
                      <span id="miradisplayprofile" class="mira-display-profile"></span>
                    </div>
                    <div id="miraClassicEditor">
                    <label id='labelcmd' style='margin-top:10px;'class="control-label">{{Ajouter un widget à l'écran}}</label>      <!-- ' -->
    				<select id="miratype">
                        <option value="nope">-- Veuillez sélectionner un type de commande --</option>
                        <option value="datetime">Date et heure</option>
        				<option value="info">Info</option>
        				<option value="button">Bouton</option>
                        <option value="multiaction">Bouton multi-actions</option>
        				<option value="toggle">Bouton basculant</option>
                        <option value="range">Curseur</option>
                        <option value="roundrange">Curseur arrondi</option>
                        <option value="thermostat">Thermostat</option>
                        <option value="camera">Caméra</option>
                        <option value="energy">Énergie · instrumentation</option>
                        <option value="media">Lecteur multimédia</option>
                        <option value="color">Couleur</option>
                        <option value="list">Liste</option>
                        <option value="listbutton">Liste vers boutons</option>
                        <!-- Types Shield conservés dans le plugin mais masqués temporairement dans l’interface -->
                        <option value="shieldrelay" hidden>Relais (Shield)</option>
                        <option value="shieldalarm" hidden>Alarme (Shield)</option>
                        <option value="shieldtoggle" hidden>Bouton basculant (Shield)</option>
    				</select>                    
                      <span style="width:90%; display:none; margin:8px auto 0;" id="mirastylezone">
                        <label class="control-label">{{Style du widget}}</label>
                        <select id="mirastyle" style="min-width:220px;"></select>
                      </span>
                      <span style="width:90%; display:none; margin:8px auto 0;" id="miralayoutzone">
                        <label class="control-label">{{Disposition}}</label>
                        <select id="miralayout" style="min-width:220px;"></select>
                      </span>
                      <span style="width:90%; display:none; margin:8px auto 0;" id="miralabelmodezone">
                        <label class="control-label">{{Affichage}}</label>
                        <select id="miralabelmode" style="min-width:220px;">
                          <option value="text">Nom</option>
                          <option value="icon">Icône</option>
                          <option value="both">Nom + icône</option>
                        </select>
                      </span>
                      <span style="width:90%; display:none; margin:8px auto 0;" id="miraiconzone">
                        <label class="control-label">{{Icône}}</label>
                        <select id="miraicon" style="min-width:220px;">
                          <option value="auto">Auto (Jeedom)</option>
                          <option value="temperature">Température</option>
                          <option value="humidity">Humidité</option>
                          <option value="pressure">Pression</option>
                          <option value="light">Luminosité</option>
                          <option value="energy">Énergie / puissance</option>
                          <option value="plug">Prise</option>
                          <option value="door">Porte / fenêtre</option>
                        </select>
                      </span>
                      <span style="width:90%;" id="miraemplacement">
                      <label class="control-label">{{Emplacement à l'écran}}</label>      <!-- ' -->
    					<select  id="emplacementSelect">
                          <option value="nope">-- Veuillez sélectionner un emplacement --</option>
                          <option value="accueil">Accueil</option>
                          <option value="temperatures">Températures</option>
                          <option value="lumieres">Lumières</option>
                          <option value="energie">Énergie</option>
                          <option value="multimedia">Multimédia</option>                        
    					</select>
                      </span>                        
                      <div class="mirainputzone"> 
                        
                        <div id="divmiraname" style="margin-top:2px; display:flex; line-height:40px; vertical-align: middle;"><div align=right style="width:30%;">{{Nom affiché}}:&nbsp;</div><input style="width:70%;" type="text" id="miraname" required minlength="1" size="12" /></div>
                        <div id="divmiramin" style="margin-top:2px; display:flex; line-height:40px; vertical-align: middle;"><div id="titlemiramin" align=right style="width:30%;">{{Valeur mini}}:&nbsp;</div><input style="width:70%;" type="number" id="miramin" size="12" onkeydown="return event.keyCode !== 69" maxlength="3" oninput="javascript: if (this.value.length > this.maxLength) this.value = this.value.slice(0, this.maxLength);"/></div>
                        <div id="divmiramax" style="margin-top:2px; display:flex; line-height:40px; vertical-align: middle;"><div align=right style="width:30%;">{{Valeur maxi}}:&nbsp;</div><input style="width:70%;" type="number" id="miramax" size="12"  onkeydown="return event.keyCode !== 69" maxlength="3" oninput="javascript: if (this.value.length > this.maxLength) this.value = this.value.slice(0, this.maxLength);"/></div>
                        <div id="divmiraval" style="margin-top:2px; display:flex; line-height:40px; vertical-align: middle;"><div id="titlemiraval" align=right style="width:30%;">{{Valeur}}:&nbsp;</div><input style="width:70%;" type="number" id="miraval" size="12" step="0.5" maxlength="6" oninput="if (this.type === 'number' && this.value.length > 6) this.value = this.value.slice(0, 6);"/></div>
                        <div id="divmiraunit" style="margin-top:2px; display:flex; line-height:40px; vertical-align: middle;"><div align=right style="width:30%;">{{Unité}}:&nbsp;</div><input style="width:70%;" type="text" id="miraunit" size="12" /></div>
                        <div id="divmiralist" style="margin-top:2px; display:flex; line-height:40px; vertical-align: middle;"><div align=right style="width:30%;">{{Contenu}}:&nbsp;</div><input placeholder="{{Séparer le contenu de la liste par | (Item1|Item2)}}" style="width:70%;" type="text" id="miralist" size="12" /></div>
                        <div id="divmirathermomodes" style="display:none;margin:3px auto; width:96%;">
                          <label for="mirathermomodes">Modes du thermostat (séparés par |)</label>
                          <input id="mirathermomodes" type="text" value="Confort|Éco|Hors gel|Arrêt" style="width:100%;">
                          <small>Après programmation, associer les commandes réelles dans les commandes Jeedom du panneau.</small>
                        </div>
                      </div>
                    
                      <div id="mirabuttonajouter" class="mirabutton" style="background-color:var(--bt-success-color); width:95%; margin-top:10px;">{{Ajouter}}</div>
                      <div id="miraboutonenvoyer" class="mirabutton" style="background-color:var(--al-warning-color); width:95%; margin-top:10px;">{{Envoyer la mise à jour au module}}</div> 
                      <table id="miratable" class="mirainputzone">
                        <thead>
                          <tr>
                            <th class="miracentre" width="8%">{{Type}}</th>
                            <th class="miracentre" width="24%">{{Affichage}}</th>
                            <th class="miracentre" width="8%">{{Emp.}}</th>
                            <th class="miracentre" width="28%">{{Nom affiché}}</th>
                            <th class="miracentre" width="7%">{{Unité}}</th>
                            <th class="miracentre" width="7%">{{Mini}}</th>
                            <th class="miracentre" width="7%">{{Maxi}}</th>
                            <th class="miracentre" width="8%">{{Valeur}}</th>
                            <th class="miracentre" width="20%">{{Disposition}}</th>
                            <th class="miracentre" width="6%"></th>

                          </tr>
                        </thead>
                        <tbody>
                          <tr>
                          </tr>
                        </tbody>
                      </table>
                    </div>

                    <div id="miraquickoptions" class="mira-quickoptions" style="display:none;" role="dialog" aria-modal="true" aria-label="Options du widget">
                      <div class="mira-quickoptions-content">
                        <strong id="miraquicktitle">Options du widget</strong>
                        <label>Nom affiché <input type="text" id="miraquickname"></label>
                        <div id="miraquickunitwrap"><label>Unité <input type="text" id="miraquickunit"></label></div>
                        <div id="miraquickminmaxwrap"><label>Minimum <input type="number" id="miraquickmin"></label><label>Maximum <input type="number" id="miraquickmax"></label></div>
                        <div id="miraquickvaluewrap"><label><span id="miraquickvaluetitle">Valeur / options</span><input type="text" id="miraquickvalue"></label></div>
                        <div id="miraquickthermomodeswrap" style="display:none"><label>Modes du thermostat <input type="text" id="miraquickthermomodes" placeholder="Confort|Éco|Hors gel|Arrêt"></label></div>

                        <div class="mira-quickoptions-actions"><button type="button" id="miraquickcancel" class="btn btn-default">Annuler</button><button type="button" id="miraquicksave" class="btn btn-success">Valider</button></div>
                      </div>
                    </div>
                    <div id="miradesigner" style="display:none;">
                      <div class="mira-designer-toolbar">
                        <div class="mira-designer-pages" role="tablist" aria-label="Page Mira">
                          <button type="button" class="mira-page-tab active" data-page="accueil">Accueil</button>
                          <button type="button" class="mira-page-tab" data-page="lumieres">Lumières</button>
                          <button type="button" class="mira-page-tab" data-page="temperatures">Températures</button>
                          <button type="button" class="mira-page-tab" data-page="energie">Énergie</button>
                          <button type="button" class="mira-page-tab" data-page="multimedia">Multimédia</button>
                        </div>
                        <div class="mira-designer-actions">
                          <button type="button" id="miradesignergrid" class="btn btn-sm btn-default active" title="Aimantation sur la grille"><i class="fas fa-th"></i> Grille</button>
                          <button type="button" id="miradesignerclose" class="btn btn-sm btn-default"><i class="fas fa-list"></i> Vue tableau</button>
                          <button type="button" id="miradesignersave" class="btn btn-sm btn-success"><i class="fas fa-save"></i> Enregistrer sur l'écran</button>
                        </div>
                      </div>

                      <div class="mira-designer-body">
                        <div class="mira-designer-stage">
                          <div id="mirascreenframe" class="mira-screen-frame">
                            <div id="mirascreenviewport" class="mira-screen-viewport">
                              <div id="mirascreencanvas" class="mira-screen-canvas"></div>
                            </div>
                          </div>
                          <div id="miradesignerhint" class="mira-designer-hint">Glisse une tuile pour la placer. Le premier écran reste entièrement visible ; le designer autorise au maximum un second écran en défilement.</div>
                        </div>

                        <aside class="mira-designer-inspector">
                          <div class="mira-inspector-title">Widget sélectionné</div>
                          <div id="mirainspectempty" class="mira-inspector-empty">Clique sur une tuile.</div>
                          <div id="mirainspectfields" style="display:none;">
                            <strong id="mirainspectname"></strong>
                            <small id="mirainspecttype"></small>
                            <div class="mira-geometry-grid">
                              <label>X <input id="mirageox" type="number" min="6" step="1"></label>
                              <label>Y <input id="mirageoy" type="number" min="8" step="1"></label>
                              <label>L <input id="mirageow" type="number" min="40" step="1"></label>
                              <label>H <input id="mirageoh" type="number" min="32" step="1"></label>
                            </div>
                            <div id="mirageounit" class="mira-geometry-unit"></div>
                            <button type="button" id="mirageoapply" class="btn btn-sm btn-primary btn-block"><i class="fas fa-check"></i> Appliquer les coordonnées</button>
                            <div id="mirageovalidation" class="mira-geometry-validation" role="status" aria-live="polite"></div>
                            <button type="button" id="miradesignerauto" class="btn btn-sm btn-default btn-block"><i class="fas fa-magic"></i> Repasser ce widget en Auto</button>
                          </div>
                        </aside>
                      </div>
                    </div>
                  </div>
                          
<div class="row row-overflow">
	<!-- Page d'accueil du plugin -->
	<div class="col-xs-12 eqLogicThumbnailDisplay">
		<div class="row">
			<div class="col-sm-10">
				<legend><i class="fas fa-cog"></i> {{Gestion}}</legend>
				<!-- Boutons de gestion du plugin -->
				<div class="eqLogicThumbnailContainer">
					<div class="cursor eqLogicAction logoPrimary" data-action="add">
						<i class="fas fa-plus-circle"></i>
						<br>
						<span>{{Ajouter}}</span>
					</div>
                    <div class="cursor eqLogicAction logoSecondary" id="bt_addmiralvgl">
						<i class='fas fa-pen '></i>
						<br>
						<span>{{Programmer un écran ESP32}}</span>
					</div>
                    <div class="cursor eqLogicAction logoSecondary" id="bt_installMiraPanel">
                        <i class="fas fa-download"></i>
                        <br>
                        <span>{{Installer Mira Panel}}</span>
                    </div>
					<div class="cursor eqLogicAction logoSecondary" data-action="gotoPluginConf">
						<i class="fas fa-wrench"></i>
						<br>
						<span>{{Configuration}}</span>
					</div>
				</div>
			</div>
			<?php
			// à conserver
			// sera afficher uniquement si l'utilisateur est en version 4.4 ou supérieur
			$jeedomVersion  = jeedom::version() ?? '0';
			$displayInfoValue = version_compare($jeedomVersion, '4.4.0', '>=');
			if ($displayInfoValue) {
			?>
				<div class="col-sm-2">
					<legend><i class=" fas fa-comments"></i> {{Community}}</legend>
					<div class="eqLogicThumbnailContainer">
						<div class="cursor eqLogicAction logoSecondary" data-action="createCommunityPost">
							<i class="fas fa-ambulance"></i>
							<br>
							<span style="color:var(--txt-color)">{{Créer un post Community}}</span>
						</div>
					</div>
				</div>
			<?php
			}
			?>
		</div>
		<legend><i class="fas fa-table"></i> {{Mes équipements}}</legend>
		<?php
		if (count($eqLogics) == 0) {
			echo '<br><div class="text-center" style="font-size:1.2em;font-weight:bold;">{{Aucun équipement Template trouvé, cliquer sur "Ajouter" pour commencer}}</div>';
		} else {
			// Champ de recherche
			echo '<div class="input-group" style="margin:5px;">';
			echo '<input class="form-control roundedLeft" placeholder="{{Rechercher}}" id="in_searchEqlogic">';
			echo '<div class="input-group-btn">';
			echo '<a id="bt_resetSearch" class="btn" style="width:30px"><i class="fas fa-times"></i></a>';
			echo '<a class="btn roundedRight hidden" id="bt_pluginDisplayAsTable" data-coreSupport="1" data-state="0"><i class="fas fa-grip-lines"></i></a>';
			echo '</div>';
			echo '</div>';
			// Liste des équipements du plugin
			echo '<div class="eqLogicThumbnailContainer">';
			foreach ($eqLogics as $eqLogic) {
				$opacity = ($eqLogic->getIsEnable()) ? '' : 'disableCard';
				echo '<div class="eqLogicDisplayCard cursor ' . $opacity . '" data-eqLogic_id="' . $eqLogic->getId() . '">';
				echo '<img src="' . $eqLogic->getImage() . '"/>';
				echo '<br>';
				echo '<b><span class="name">' . $eqLogic->getHumanName(true, true) . '</span></b>';
				echo '<span style="font-size: 12px;">'.$eqLogic->getConfiguration('adresseip'). '</span>';
				
				echo '<span class="hiddenAsCard displayTableRight hidden">';
				echo ($eqLogic->getIsVisible() == 1) ? '<i class="fas fa-eye" title="{{Equipement visible}}"></i>' : '<i class="fas fa-eye-slash" title="{{Equipement non visible}}"></i>';
				echo '</span>';
				echo '</div>';
              	
              $type_cmd = $eqLogic->getConfiguration('cmd_type');
			}
			echo '</div>';
		}
		?>
          
          
	</div> <!-- /.eqLogicThumbnailDisplay -->

	<!-- Page de présentation de l'équipement -->
	<div class="col-xs-12 eqLogic" style="display: none;">
		<!-- barre de gestion de l'équipement -->
		<div class="input-group pull-right" style="display:inline-flex;">
			<span class="input-group-btn">
				<!-- Les balises <a></a> sont volontairement fermées à la ligne suivante pour éviter les espaces entre les boutons. Ne pas modifier -->
				<a class="btn btn-sm btn-default eqLogicAction roundedLeft" data-action="configure"><i class="fas fa-cogs"></i><span class="hidden-xs"> {{Configuration avancée}}</span>
				</a><a class="btn btn-sm btn-default eqLogicAction" data-action="copy"><i class="fas fa-copy"></i><span class="hidden-xs"> {{Dupliquer}}</span>
				</a><a class="btn btn-sm btn-success eqLogicAction" data-action="save"><i class="fas fa-check-circle"></i> {{Sauvegarder}}
				</a><a class="btn btn-sm btn-danger eqLogicAction roundedRight" data-action="remove"><i class="fas fa-minus-circle"></i> {{Supprimer}}
				</a>
			</span>
		</div>
		<!-- Onglets -->
		<ul class="nav nav-tabs" role="tablist">
			<li id="mira_retour" role="presentation"><a href="#" class="eqLogicAction" aria-controls="home" role="tab" data-toggle="tab" data-action="returnToThumbnailDisplay"><i class="fas fa-arrow-circle-left"></i></a></li>
			<li id="mira_equipement" role="presentation" class="active"><a href="#eqlogictab" aria-controls="home" role="tab" data-toggle="tab"><i class="fas fa-tachometer-alt"></i> {{Equipement}}</a></li>
			<li id="mira_commandes" role="presentation" ><a href="#commandtab" aria-controls="home" role="tab" data-toggle="tab"><i class="fas fa-list"></i> {{Commandes}}</a></li>
		</ul>
		<div class="tab-content">
			<!-- Onglet de configuration de l'équipement -->
			<div role="tabpanel" class="tab-pane active" id="eqlogictab">
				<!-- Partie gauche de l'onglet "Equipements" -->
				<!-- Paramètres généraux et spécifiques de l'équipement -->
				<form class="form-horizontal">
					<fieldset>
						<div class="col-lg-6">
							<legend><i class="fas fa-wrench"></i> {{Paramètres généraux}}</legend>
							<div class="form-group">
								<label class="col-sm-4 control-label">{{Nom de l'équipement}}</label>
								<div class="col-sm-6">
									<input type="text" class="eqLogicAttr form-control" data-l1key="id" style="display:none;">
									<input type="text" class="eqLogicAttr form-control" data-l1key="name" placeholder="{{Nom de l'équipement}}">
								</div>
							</div>
							<div class="form-group">
								<label class="col-sm-4 control-label">{{Objet parent}}</label>
								<div class="col-sm-6">
									<select id="sel_object" class="eqLogicAttr form-control" data-l1key="object_id">
										<option value="">{{Aucun}}</option>
										<?php
										$options = '';
										foreach ((jeeObject::buildTree(null, false)) as $object) {
											$options .= '<option value="' . $object->getId() . '">' . str_repeat('&nbsp;&nbsp;', $object->getConfiguration('parentNumber')) . $object->getName() . '</option>';
										}
										echo $options;
										?>
									</select>
								</div>
							</div>
							<div class="form-group">
								<label class="col-sm-4 control-label">{{Catégorie}}</label>
								<div class="col-sm-6">
									<?php
									foreach (jeedom::getConfiguration('eqLogic:category') as $key => $value) {
										echo '<label class="checkbox-inline">';
										echo '<input type="checkbox" class="eqLogicAttr" data-l1key="category" data-l2key="' . $key . '" >' . $value['name'];
										echo '</label>';
									}
									?>
								</div>
							</div>
							<div class="form-group">
								<label class="col-sm-4 control-label">{{Options}}</label>
								<div class="col-sm-6">
									<label class="checkbox-inline"><input type="checkbox" class="eqLogicAttr" data-l1key="isEnable" checked>{{Activer}}</label>
									<label class="checkbox-inline"><input type="checkbox" class="eqLogicAttr" data-l1key="isVisible" checked>{{Visible}}</label>
								</div>
							</div>

							<legend><i class="fas fa-cogs"></i> {{Paramètres spécifiques}}</legend>
                                      
                            
							<div class="form-group">
								<label class="col-sm-4 control-label">{{Adresse IP}}
									<sup><i class="fas fa-question-circle tooltips" title="{{Renseignez l'adresse IP de l'ESP32}}"></i></sup>
								</label>
								<div class="col-sm-6">
									<input type="text" class="eqLogicAttr form-control" data-l1key="configuration" data-l2key="adresseip" placeholder="{{Adresse IP}}">
								</div>
							</div>
							<div class="form-group">
								<label class="col-sm-4 control-label"> {{Mot de passe}}
									<sup><i class="fas fa-question-circle tooltips" title="{{Renseignez le mot de passe}}"></i></sup>
								</label>
								<div class="col-sm-6">
									<input type="text" class="eqLogicAttr form-control inputPassword" data-l1key="configuration" data-l2key="password">
								</div>
							</div>
                                      
                            <div class="form-group">
								<label class="col-sm-4 control-label"> {{Référence du module}}
									<sup><i class="fas fa-question-circle tooltips" title="{{La référence sera complétée automatiquement si elle a été paramétrée sur l'ESP32}}"></i></sup>
								</label>
								<div class="col-sm-6">
									<input type="text" disabled class="eqLogicAttr form-control" data-l1key="configuration" data-l2key="module">
								</div>
							</div>
                                      
                            <div class="form-group">
								<label class="col-sm-4 control-label"> {{Ajouter les widgets mira}}
									<sup><i class="fas fa-question-circle tooltips" title="{{Si la case est cochée, les widgets Mira Panel seront ajoutés automatiquement}}"></i></sup>
								</label>
								<div class="col-sm-6">
									<input type="checkbox" value="1" class="eqLogicAttr form-control" data-l1key="configuration" data-l2key="widgets" checked>
								</div>
							</div>
                            
                            <legend><i class="fas fa-star"></i> {{Navigation — favoris}}</legend>
                            <div class="alert alert-info" style="margin:8px 12px 14px;">
                                {{Sélectionnez trois raccourcis fixes. Le bouton Pages reste toujours accessible. Enregistrez l'équipement pour transmettre les favoris à l'écran (firmware 0.7.2 minimum).}}
                            </div>
                            <?php
                            $miraNavOptions = array(
                                '0' => 'Accueil',
                                '1' => 'Lumières',
                                '2' => 'Températures',
                                '4' => 'Énergie',
                                '5' => 'Multimédia',
                                '3' => 'Paramètres',
                                '255' => 'Aucun'
                            );
                            for ($miraSlot = 1; $miraSlot <= 3; ++$miraSlot) {
                                echo '<div class="form-group">';
                                echo '<label class="col-sm-4 control-label">Favori ' . $miraSlot . '</label>';
                                echo '<div class="col-sm-6">';
                                echo '<select class="eqLogicAttr form-control mira-nav-favorite" data-l1key="configuration" data-l2key="nav_fav' . $miraSlot . '">';
                                foreach ($miraNavOptions as $id => $label) {
                                    echo '<option value="' . $id . '"' . ((string)($miraSlot - 1) === $id ? ' selected' : '') . '>' . htmlspecialchars($label, ENT_QUOTES, 'UTF-8') . '</option>';
                                }
                                echo '</select></div></div>';
                            }
                            ?>
                            <div class="form-group">
                                <div class="col-sm-8 col-sm-offset-4">
                                    <button type="button" id="miraSyncFavorites" class="btn btn-default btn-sm">
                                        <i class="fas fa-sync-alt"></i> {{Réenvoyer les favoris}}
                                    </button>
                                    <small id="miraFavoritesSyncStatus" class="help-block">{{Utile après un redémarrage ou si l'écran était déconnecté.}}</small>
                                </div>
                            </div>
                             <legend><i class="fas fa-eye"></i> {{Pages affichées}}</legend>
                            <p class="help-block" style="margin-left:10px;">{{Masquer une page la retire du menu Pages et empêche de la sélectionner depuis les favoris. Aucun widget ni aucune configuration ne sera supprimé. Accueil reste toujours disponible.}}</p>
                            <?php
                            foreach (array('1'=>'Lumières','2'=>'Températures','3'=>'Paramètres',
                                           '4'=>'Énergie','5'=>'Multimédia') as $miraPageId=>$miraPageName) {
                                echo '<div class="form-group">';
                                echo '<label class="col-sm-4 control-label">'.htmlspecialchars($miraPageName,ENT_QUOTES,'UTF-8').'</label>';
                                echo '<div class="col-sm-6">';
                                echo '<label class="checkbox-inline"><input type="checkbox" value="1" class="eqLogicAttr" data-l1key="configuration" data-l2key="nav_show'.$miraPageId.'" checked> {{Afficher dans Mira}}</label>';
                                echo '</div></div>';
                            }
                            ?>
                            <legend><i class="fab fa-spotify"></i> {{Lecteur multimédia}}</legend>
                            <div class="form-group">
                              <label class="col-sm-4 control-label">{{Lecteur Jeedom à associer}}</label>
                              <div class="col-sm-6">
                                <select class="eqLogicAttr form-control" data-l1key="configuration" data-l2key="media_source_eqid" id="miraMediaSourceEq">
                                  <option value="">{{Configuration manuelle}}</option>
                                  <?php
                                  // Some Jeedom installs resolve both spotifyConnect and
                                  // spotifyconnect to the very same equipment family.
                                  // Never duplicate an equipment in Mira's source menu.
                                  $miraSeenMediaSourceIds=array();
                                  foreach (array('spotify','spotifyconnect','spotifyConnect') as $miraSpotifyType) {
                                    foreach ((array)eqLogic::byType($miraSpotifyType) as $miraSpotifyEq) {
                                      if (!is_object($miraSpotifyEq)) continue;
                                      $miraSpotifyEqId=(int)$miraSpotifyEq->getId();
                                      if ($miraSpotifyEqId<1 || isset($miraSeenMediaSourceIds[$miraSpotifyEqId])) continue;
                                      $miraSeenMediaSourceIds[$miraSpotifyEqId]=true;
                                      $miraSpotifyLabel=strtolower($miraSpotifyType)==='spotify' ? 'Spotify' : 'Spotify Connect';
                                      echo '<option value="'.$miraSpotifyEqId.'">'
                                          .htmlspecialchars($miraSpotifyLabel.' · '.$miraSpotifyEq->getHumanName(),ENT_QUOTES,'UTF-8')
                                          .'</option>';
                                    }
                                  }
                                  ?>
                                </select>
                                <small class="help-block">{{Choisissez le lecteur Spotify piloté par Mira. Les commandes sont associées automatiquement.}}</small>
                              </div>
                            </div>

                            

                            <div class="form-group">
								<label class="col-sm-4 control-label"> {{Surveiller l'écran (15 min)}}
									<sup><i class="fas fa-question-circle tooltips" title="{{Vérifie la connexion toutes les 15 minutes et rafraîchit les informations lorsque l'écran répond.}}"></i></sup>
								</label>
								<div class="col-sm-6">
									<input type="checkbox" value="1" class="eqLogicAttr form-control" data-l1key="configuration" data-l2key="ping" checked>
								</div>
							</div>
                                      
                                      
                                      
                         </div>           
                                      
							

						<!-- Partie droite de l'onglet "Équipement" -->
						<!-- Affiche un champ de commentaire par défaut mais vous pouvez y mettre ce que vous voulez -->
						<div class="col-lg-6">
							<legend><i class="fas fa-info"></i> {{Informations}}</legend>
							<div class="form-group">
								<label class="col-sm-4 control-label">{{Description}}</label>
								<div class="col-sm-6">
									<textarea class="form-control eqLogicAttr autogrow" data-l1key="comment"></textarea>
								</div>
							</div>
                            <legend><i class="fas fa-at"></i> {{URL de retour}}</legend>
							<div class="form-group">
								<div class="alert alert-info col-xs-10 col-xs-offset-1 text-center callback">
									<span>
										<?php echo network::getNetworkAccess('external') . '/core/api/jeeApi.php?plugin=mira&type=event&apikey=' . jeedom::getApiKey($plugin->getId()) . '&id=#cmd_id# (&value=#value#)';
										?>
									</span>
								</div>
							</div>
						</div>
					</fieldset>
				</form>
			</div><!-- /.tabpanel #eqlogictab-->

			<!-- Onglet des commandes équipement -->
            
			<div role="tabpanel" class="tab-pane" id="commandtab">
				<div class="input-group pull-right" style="display:inline-flex;margin-top:5px;">					
				</div>
				<br>
                <p style="color : #4894E1;">{{Vous pouvez réorganiser les lignes en cliquant sur l'entête des colonnes}}</p>   <!-- ' -->                
				<div class="table-responsive">
					<table id="table_cmd" class="sortable table table-bordered table-condensed">
						<thead>
							<tr>
								<th class="sorttable_numeric" style="cursor : pointer; width:70px;">
                                   <button>
                                      ID
                                      <span aria-hidden="true"></span>
                                   </button>
                                </th>
                                      
                                <th style="cursor : pointer; width:200px;">
                                   <button>
                                      Nom
                                      <span aria-hidden="true"></span>
                                   </button>
                                </th>
                                      
                                <th class="sorttable_numeric" style="cursor : pointer; width:80px;">
                                   <button>
                                      mira
                                      <span aria-hidden="true"></span>
                                   </button>
                                </th>
                                      
                                <th style="cursor : pointer; width:80px;">
                                   <button>
                                      Unités
                                      <span aria-hidden="true"></span>
                                   </button>
                                </th>
                                
                                <th class="sorttable_numeric" style="cursor : pointer; width:100px;">
                                   <button>
                                      Afficher
                                      <span aria-hidden="true"></span>
                                   </button>
                                </th>
                                
                                <th class="sorttable_numeric" style="cursor : pointer; width:100px;">
                                   <button>
                                      Historiser
                                      <span aria-hidden="true"></span>
                                   </button>
                                </th>
                                
                                <th style="cursor : pointer; width:150px;">
                                   <button>
                                      Type
                                      <span aria-hidden="true"></span>
                                   </button>
                                </th>
                                
                                <th style="width:138px; min-width:115px;">Action</th>

                                <th style="cursor : pointer;">
                                   <button>
                                      Paramètres
                                      <span aria-hidden="true"></span>
                                   </button>
                                </th>
                  				
                                <th class="sorttable_numeric" style="cursor : pointer;">
                                   <button>
                                      Etat
                                      <span aria-hidden="true"></span>
                                   </button>
                                </th>
                                
                                <th style="cursor : pointer; width:90px;">
                                   <button>
                                      {{Actions}}
                                      <span aria-hidden="true"></span>
                                   </button>
                                </th>
                                
							</tr>
						</thead>
						<tbody>
						</tbody>
					</table>
				</div>
			</div><!-- /.tabpanel #commandtab-->
			
		</div><!-- /.tab-content -->
	</div><!-- /.eqLogic -->
</div><!-- /.row row-overflow -->

<!-- Inclusion du fichier javascript du plugin (dossier, nom_du_fichier, extension_du_fichier, id_du_plugin) -->
<?php include_file('desktop', 'mira', 'js', 'mira'); ?>
<?php include_file('desktop', 'mira', 'css', 'mira'); ?>  
<?php include_file('desktop', 'sort', 'js', 'mira'); ?>
<!-- Inclusion du fichier javascript du core - NE PAS MODIFIER NI SUPPRIMER -->
<?php include_file('core', 'plugin.template', 'js'); ?>