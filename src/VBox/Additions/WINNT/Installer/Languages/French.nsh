; $Id: French.nsh 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; NLS for French language.
;

;
; Copyright (C) 2006-2024 Oracle and/or its affiliates.
;
; This file is part of VirtualBox base platform packages, as
; available from https://www.virtualbox.org.
;
; This program is free software; you can redistribute it and/or
; modify it under the terms of the GNU General Public License
; as published by the Free Software Foundation, in version 3 of the
; License.
;
; This program is distributed in the hope that it will be useful, but
; WITHOUT ANY WARRANTY; without even the implied warranty of
; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
; General Public License for more details.
;
; You should have received a copy of the GNU General Public License
; along with this program; if not, see <https://www.gnu.org/licenses>.
;
; SPDX-License-Identifier: GPL-3.0-only
;

LangString VBOX_TEST ${LANG_FRENCH}                                 "Ceci est un message de test de $(^Name)!"

LangString VBOX_NOADMIN ${LANG_FRENCH}                              "Vous avez besoin de droits d'administrateur pour (dés)installer $(^Name)!$\r$\nCe programme d'installation se terminera maintenant."

LangString VBOX_NOTICE_ARCH_X86 ${LANG_FRENCH}                      "Cette application peut seulement être executée sur des systèmes Windows 32-bit. Veuillez installer la version 64-bit de $(^Name)!"
LangString VBOX_NOTICE_ARCH_AMD64 ${LANG_FRENCH}                    "Cette application peut seulement être executée sur des systèmes Windows 64-bit. Veuillez installer la version 32-bit de $(^Name)!"
LangString VBOX_NT4_NO_SP6 ${LANG_FRENCH}                           "Le programme d'installation a détécté que vous utilisez Windows NT4 sans Service Pack 6.$\r$\nNous vous conseillons d'installer ce Service Pack avant de continuer. Désirez vous cependant continuer?"

LangString VBOX_PLATFORM_UNSUPPORTED ${LANG_FRENCH}                 "Les Additions invité ne sont pas encore supportés sur cette plateforme!"

LangString VBOX_SUN_FOUND ${LANG_FRENCH}                            "Une ancienne version des Additions invité Sun est installée dans cette machine virtuelle. Les Additions invité actuelles ne peuvent être installées avant que cette version ne soit désinstallée.$\r$\n$\r$\nVoulez-vous désinstaller l'ancienne version maintenant?"
LangString VBOX_SUN_ABORTED ${LANG_FRENCH}                          "Le programme ne peut pas continuer avec l'installation des Additions invité.$\r$\nVeuillez désinstaller d'abord les anciennes Additions Sun!"

LangString VBOX_INNOTEK_FOUND ${LANG_FRENCH}                        "Une ancienne version des Additions invité est installée dans cette machine virtuelle. Les Additions invité actuelles ne peuvent être installées avant que cette version ne soit désinstallée.$\r$\n$\r$\nVoulez-vous désinstaller l'ancienne version maintenant?"
LangString VBOX_INNOTEK_ABORTED ${LANG_FRENCH}                      "Le programme ne peut pas continuer avec l'installation des Additions invité.$\r$\nVeuillez désinstaller d'abord les anciennes Additions!"

LangString VBOX_UNINSTALL_START ${LANG_FRENCH}                      "Choisissez OK pour démarrer la désinstallation.$\r$\nLe processus nécessitera quelque temps et se déroulera en arrière-plan."
LangString VBOX_UNINSTALL_REBOOT ${LANG_FRENCH}                     "Nous vous conseillons fortement de redémarer cette machine virtuelle avant d'installer la nouvelle version des Additions invité.$\r$\nVeuillez recommencer l'installation des Additions après le redémarrage.$\r$\n$\r$\nRedémarrer maintenant?"

LangString VBOX_COMPONENT_MAIN ${LANG_FRENCH}                       "Additions invité VirtualBox"
LangString VBOX_COMPONENT_MAIN_DESC ${LANG_FRENCH}                  "Fichiers prinipaux des Additions invité VirtualBox"

LangString VBOX_COMPONENT_AUTOLOGON ${LANG_FRENCH}                  "Support identification automatique"
LangString VBOX_COMPONENT_AUTOLOGON_DESC ${LANG_FRENCH}             "Active l'identification automatique dans l'invité"
LangString VBOX_COMPONENT_AUTOLOGON_WARN_3RDPARTY ${LANG_FRENCH}    "Un composant permettant l'identification automatique est déjà installé.$\r$\nSi vous le remplacé avec le composant issue de VirtualBox, cela pourrait déstabiliser le système.$\r$\nDésirez-vous cependant continuer?"

LangString VBOX_COMPONENT_D3D  ${LANG_FRENCH}                       "Support Direct3D pour invités (experimental)"
LangString VBOX_COMPONENT_D3D_DESC  ${LANG_FRENCH}                  "Active le support Direct3D pour invités (experimental)"
LangString VBOX_COMPONENT_D3D_NO_SM ${LANG_FRENCH}                  "Windows ne fonctionne pas en mode sans échec.$\r$\nDe ce fait, le support D3D ne peut être installé."
LangString VBOX_COMPONENT_D3D_NOT_SUPPORTED ${LANG_FRENCH}          "Le support invité pour Direct3D n'est pas disponible sur Windows $g_strWinVersion!"
;LangString VBOX_COMPONENT_D3D_HINT_VRAM ${LANG_FRENCH}              "Veuillez noter que l'utilisation de l'accélération 3D nécécssite au moins 128 MB de mémoire vidéo ; pour un utilisation avec plusieurs écrans nous recommandons  d'affecter 256 MB.$\r$\n$\r$\nSi nécéssaire vous pouvez changer la taille du mémoire vidéo dans la sous-section $\"Affichage$\" des paramètres de la machine virtuelle."
LangString VBOX_COMPONENT_D3D_INVALID ${LANG_FRENCH}                "Le programme d'installation a détecté une installation DirectX corrompue ou invalide.$\r$\n$\r$\nAfin d'assurer le bon fonctionnement du support DirectX, nous conseillons de réinstaller le moteur d'exécution DirectX.$\r$\n$\r$\nDésirez-vous cependant continuer?"
LangString VBOX_COMPONENT_D3D_INVALID_MANUAL ${LANG_FRENCH}         "Voulez-vous voir le manuel d'utilisateur VirtualBox pour chercher une solution?"

LangString VBOX_COMPONENT_STARTMENU ${LANG_FRENCH}                  "Start menu entries"
LangString VBOX_COMPONENT_STARTMENU_DESC ${LANG_FRENCH}             "Creates entries in the start menu"

LangString VBOX_WFP_WARN_REPLACE ${LANG_FRENCH}                     "Le programme d'installation vient de remplacer certains fichiers systèmes afin de faire fonctionner correctement ${PRODUCT_NAME}.$\r$\nPour le cas qu'un avertissement de la Protection de fichiers Windows apparaisse, veuiller l'annuler sans restaurer les fichiers originaux!"
LangString VBOX_REBOOT_REQUIRED ${LANG_FRENCH}                      "Le système doit être redémarré pourque les changements prennent effet. Redémarrer Windows maintenant?"

LangString VBOX_EXTRACTION_COMPLETE ${LANG_FRENCH}                  "$(^Name): Les fichiers ont été extrait avec succès dans $\"$INSTDIR$\"!"

LangString VBOX_ERROR_INST_FAILED ${LANG_FRENCH}                    "Une erreur est survenue pendant l'installation!$\r$\nVeuillez consulter le fichier log '$INSTDIR\install_ui.log' pour plus d'informations."
LangString VBOX_ERROR_OPEN_LINK ${LANG_FRENCH}                      "Impossible d'ouvrir le lien dans le navigateur par défaut."

LangString VBOX_UNINST_CONFIRM ${LANG_FRENCH}                       "Voulez-vous vraiment désinstaller $(^Name)?"
LangString VBOX_UNINST_SUCCESS ${LANG_FRENCH}                       "$(^Name) ont été désinstallés."
LangString VBOX_UNINST_INVALID_D3D ${LANG_FRENCH}                   "Installation incorrecte du support Direct3D detectée; une désinstallation ne sera pas tentée."
LangString VBOX_UNINST_UNABLE_TO_RESTORE_D3D ${LANG_FRENCH}         "La restauration des fichiers originaux Direct3D a echoué. Veuillez réinstaller DirectX."
