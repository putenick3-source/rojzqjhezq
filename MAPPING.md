# Mapping du pilote — ordre et vérifications

## Ordre obligatoire : mapper AVANT de lancer le jeu

Un `EasyAntiCheat_EOSSys` résident fait échouer le chargement du pilote vulnérable
`iqvw64e.sys` (utilisé par le mappeur) avec `0xC0000022` (STATUS_ACCESS_DENIED) :
le mappeur abandonne avant même de toucher l'image à mapper. Jeu fermé — donc EAC
déchargé — le chargement réussit. Ce n'est ni CI, ni HVCI, ni la blocklist :
signature Intel valide, `VulnerableDriverBlocklistEnable = 0`,
`HypervisorEnforcedCodeIntegrity\Enabled = 0`.

## A vérifier avant chaque mapping

1. aucun processus `ROB RUN.exe` ;
2. aucun pilote `EasyAntiCheat_EOSSys` (ni processus `EasyAnti*`) ;
3. aucun service `iqvw64e` : `sc query iqvw64e` doit répondre « erreur 1060 ».

Si l'un est présent : fermer le jeu (EAC se décharge seul), et redémarrer si besoin.

## Outils

- `tools\test_kd2.ps1 -NoWait` : mappe `tools\kd2\kernel-mode.sys` avec
  `tools\kd2\kd2.exe` (lancé sans argument, depuis `tools\kd2`), sans attendre le
  jeu. Sans le commutateur : attente de `ROB RUN.exe` (1 s / 300 s), puis 15 s de
  marge, puis mapping. Codes de sortie : `0` mappé, `1` timeout, `2` terminal non
  élevé, `3` prérequis manquant, `4` kd2 a échoué. Journal : `build_driver\test_kd2.log`.
- `tools\test_min.ps1` : même déroulé via kdmapper, pour la **variante minimale**
  (`RR_MINIMAL`, aucun device) et sa cible CMake `RrSvc_min`.

## Nom de device

Le pilote choisit son nom au chargement : device `\Device\Xkq<suffixe>` et lien DOS
`\DosDevices\Xkq<suffixe>`, le préfixe restant stable pour permettre la recherche.
On le retrouve en énumérant le namespace DOS (`QueryDosDevice` avec un nom NULL) et
en filtrant sur le préfixe `Xkq`, puis en ouvrant `\\.\<nom>` :

    QueryDosDevice  -> un seul nom en Xkq*   (ex. Xkqywvku6p5 -> \Device\Xkqywvku6p5)
    \\.\Xkqywvku6p5 -> CreateFileW OK        (symlink fonctionnel)

## Quirk kd2.cfg : le nettoyage des traces ne tourne pas

`kd2_cleanup_enabled()` teste la présence de la chaîne `cleanup=0` dans **chaque**
ligne du fichier : la ligne de commentaire `; cleanup=0 skips it so the` suffit donc
à désactiver le nettoyage, malgré la ligne active `cleanup=1`. Le log affiche
`Trace cleanup disabled by kd2.cfg` et aucun édit PiDDB / hash bucket /
MmUnloadedDrivers n'est fait. Aucun impact sur le mapping lui-même.
