# ESP32 Remote — v1.4.0

ESP32 Remote est un firmware pour ESP32-S2/S3 qui expose une interface Web locale, des commandes MQTT et des fonctions HID USB afin de piloter une télévision depuis le réseau, à partir d'une page locale, via Domoticz ou via Home Assistant.
L'API de ma TV Philips plantant régulièrement, ce firmware émule un clavier HID physique branché en USB.

> ⚠️ **Matériel requis** : ESP32-S2 ou ESP32-S3 (USB OTG natif). Un ESP32 classique (sans port USB natif) ne supporte pas `USBHIDKeyboard` et ne compilera pas ce sketch.

---

## Captures d'écran

<table>
  <tr>
    <td align="center"><strong>Interface principale</strong></td>
    <td align="center"><strong>Gestion des macros</strong></td>
  </tr>
  <tr>
    <td><img src="docs/ESP32Remote.png" alt="Interface principale ESP32 Remote" width="280"/></td>
    <td><img src="docs/macros.png" alt="Page macros" width="280"/></td>
  </tr>
  <tr>
    <td align="center"><strong>Configuration</strong></td>
    <td align="center"><strong>Mise à jour OTA</strong></td>
  </tr>
  <tr>
    <td><img src="docs/config.png" alt="Page de configuration" width="280"/></td>
    <td><img src="docs/OTA.png" alt="Page OTA" width="280"/></td>
  </tr>
</table>

---

## Fonctionnalités

- Interface Web locale avec une page principale `/`, une page de configuration `/config`, une page de gestion des macros `/macros` et une page de mise à jour firmware `/update`.
- Envoi de commandes HID clavier, media et système via `USBHIDKeyboard`, `USBHIDConsumerControl` et `USBHIDSystemControl`.
- Pilotage par HTTP et MQTT.
- Moteur de macros : séquences de commandes avec délais, stockées en NVS.
- Envoi d'un code HID personnalisé (`key:<code>`).
- **Saisie de texte libre** (`text:`) avec émulation de frappe humaine caractère par caractère.
- **Délais HID configurables** depuis l'interface web pour s'adapter à la réactivité de n'importe quelle TV ou box (Fire TV, etc.).
- Architecture **Single-Thread non-bloquante** (files d'attente) optimisée pour la stabilité des puces monocœur (ESP32-S2) même sous de lourdes requêtes HTML ou MQTT.
- Mise à jour OTA par interface Web (glisser-déposer d'un `.bin`) avec suivi de progression et redémarrage automatique.
- Support d'`ArduinoOTA` (mise à jour réseau depuis l'IDE).
- Découverte automatique Home Assistant (MQTT auto-discovery) et publication de télémétrie.
- Mode point d'accès de secours si la connexion Wi-Fi échoue.

---

## 💡 Astuces & Compatibilité Fire TV / Android TV

Si vous contrôlez une box (comme une **Fire TV**) à travers le câble HDMI de votre téléviseur (protocole HDMI-CEC), la saisie de lettres de clavier risque d'être filtrée par la TV. 
- Les commandes de navigation (flèches), médias (Volume, Play/Pause) et les **chiffres (0-9)** passeront sans aucun problème via le HDMI-CEC.
- La touche "**Retour Arrière**" (Backspace) du pavé numérique web envoie intentionnellement la commande média système `CONSUMER_CONTROL_BACK` pour garantir le fonctionnement du bouton retour natif sur les box TV.
- Pour une saisie de texte alphabétique 100% fonctionnelle, le câble USB (OTG) de l'ESP32 doit idéalement être branché *directement* sur le port USB de la box TV, et non sur le port de la télévision.

---

## Démarrage

Au démarrage, le firmware charge sa configuration depuis la NVS avec `Preferences`, tente une connexion Wi-Fi en mode station, puis initialise le serveur Web, mDNS, MQTT et OTA si le Wi-Fi est disponible.
Si la connexion Wi-Fi échoue, l'appareil bascule en mode point d'accès avec le SSID `ESP32-Remote-Setup`, mot de passe `esp32setup`, et un portail de configuration accessible sur `192.168.4.1`.
Le nom d'hôte par défaut est `ESP32-Remote` ; l'interface est accessible via `http://ESP32-Remote.local` quand mDNS fonctionne.

---

## Configuration

La page `/config` permet de régler :

- **Wi-Fi** : SSID, mot de passe, IP fixe ou DHCP (adresse, passerelle, masque, DNS).
- **MQTT** : broker, port, identifiants, Client ID, topics de commande et de statut.
- **Appareil** : hostname (utilisé aussi pour mDNS et ArduinoOTA), langue de l'interface, et **réglage fin des délais HID** (temps d'appui et pause entre les touches).

La configuration est enregistrée en NVS, puis l'ESP32 redémarre automatiquement après sauvegarde.
Une réinitialisation complète est disponible via `POST /config/reset`.

---

## Commandes disponibles

Le moteur de dispatch accepte les commandes suivantes, déclenchables par HTTP ou MQTT.

### Navigation
| Commande | Action |
|---|---|
| `up` | Flèche haut |
| `down` | Flèche bas |
| `left` | Flèche gauche |
| `right` | Flèche droite |
| `enter` | Entrée / OK |
| `esc` | Retour / Echap |
| `space` | Espace |

### Système
| Commande | Action |
|---|---|
| `power` | Touche Power |
| `home` | Touche Home |
| `wake` | Wake (Consumer Wake-Up) |

### Volume & Lecture
| Commande | Action |
|---|---|
| `volup` | Volume + |
| `voldown` | Volume - |
| `mute` | Muet |
| `playpause` | Lecture / Pause |
| `next` | Piste suivante |
| `previous` | Piste précédente |
| `volhuit` | Régler le volume à 8 (depuis 0) |
| `vol:<n>` | Régler le volume à `n` (0 – 20) |

### Sources
| Commande | Action |
|---|---|
| `antenne` | Sélectionner la source Antenne |
| `miracast` | Sélectionner la source Miracast |

### Modes sonores
| Commande | Action |
|---|---|
| `son` | Ouvrir le sous-menu son |
| `sonoriginal` | Mode Original |
| `sondivertissement` | Mode Divertissement |
| `sonmusique` | Mode Musique |
| `sonmusiquespatiale` | Mode Musique Spatiale |
| `sondialogue` | Mode Dialogue |
| `sonpersonnel` | Mode Personnel |

### Pavé numérique
`digit0` à `digit9` — envoie le chiffre correspondant.

### Codes bruts & Saisie
- `key:<code>` — envoie le code Consumer Control (décimal ou `0x…`).
- `text:<chaine>` — tape la chaîne de caractères (émulation de frappe au clavier).

### Macros
- `macro:<nom>` — exécute la macro portant ce nom.

---

## Macros

Les macros sont stockées en NVS dans un espace séparé, avec un nom et une suite d'étapes au format `commande:délai_ms` séparées par des virgules.
Le firmware gère jusqu'à **10 macros**. L'étape spéciale `delay:<ms>` insère une pause sans envoyer de touche.

L'interface `/macros` permet de créer, modifier, exécuter et supprimer des macros. Les routes dédiées sont :

- `GET /macrolist` — liste JSON des macros.
- `POST /macrosave` — enregistrement d'une macro.
- `POST /macrorun` — exécution d'une macro.
- `POST /macrodelete` — suppression d'une macro.

À chaque ajout ou suppression, la découverte Home Assistant est republiée pour refléter la liste courante.

---

## MQTT et Home Assistant

Le firmware souscrit au topic de commande configuré et publie les retours d'état sur le topic de statut configuré.
Il publie également :

- Un état LWT `online` / `offline` (retained).
- Une télémétrie périodique : RSSI Wi-Fi, uptime, adresse IP, version du firmware (`1.4.0`), nombre de démarrages.

Des entités Home Assistant sont générées automatiquement (MQTT auto-discovery) pour les boutons de commande, les capteurs de télémétrie et les macros enregistrées.

---

## Mise à jour OTA

La page `/update` permet d'envoyer un fichier firmware `.bin` par glisser-déposer ou sélection manuelle, puis d'écrire le firmware avec `Update.begin` / `Update.write` / `Update.end(true)`.
En cas de succès, le firmware répond `OK` puis redémarre automatiquement.

Le binaire se génère depuis Arduino IDE via **Sketch → Export Compiled Binary**.
La mise à jour par `ArduinoOTA` (protocole réseau) est aussi supportée en parallèle.

---

## Routes principales

| Méthode | Route | Description |
|---|---|---|
| `GET` | `/` | Interface principale de pilotage |
| `GET` | `/config` | Lecture de la configuration |
| `POST` | `/config` | Sauvegarde de la configuration |
| `POST` | `/config/reset` | Réinitialisation de la configuration |
| `GET` | `/macros` | Interface de gestion des macros |
| `GET` | `/macrolist` | Liste des macros (JSON) |
| `POST` | `/macrosave` | Enregistrement d'une macro |
| `POST` | `/macrorun` | Exécution d'une macro |
| `POST` | `/macrodelete` | Suppression d'une macro |
| `GET` | `/update` | Interface OTA |
| `POST` | `/update` | Upload du firmware |
| `GET` | `/mqttstatus` | État de la connexion MQTT (JSON) |
| `POST` | `/sendkey` | Envoi d'un code HID personnalisé |
| `POST` | `/sendtext` | Envoi d'une chaîne de caractères libre |
| `GET` | `/vol` | Réglage volume précis (`?val=0`–`20`) |
| `GET/POST` | `/*` (AP mode) | Portail captif de configuration |
