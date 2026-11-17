# Driver de lecture noyau (RrSvc.sys)

Driver noyau mappé manuellement, strictement en lecture, qui remplace
`ReadProcessMemory` dans l'overlay. Aucun hook, aucun callback, aucun thread :
le seul contrat exposé est un device `\Device\RrSvc94` et deux IOCTL
(`RR_IOCTL_BATCH`, `RR_IOCTL_MODULE`). Si le device n'est pas joignable,
l'overlay retombe silencieusement sur RPM.

> Le driver est hors CMake : il ne se construit que par `driver/build.bat`.

## Prérequis

1. **Visual Studio 2022** (Community suffit) avec la charge « Développement
   Desktop C++ » et le pack x64.
2. **Windows Driver Kit** correspondant au SDK installé
   (`winget install Microsoft.WindowsSDK` ou le programme d'installation du
   SDK/WDK). C'est le composant qui fournit `km\` (en-têtes et libs noyau).
3. **kdmapper** (ou équivalent) pour le mapping manuel.

Le batch choisit automatiquement le kit le plus récent qui possède
`Lib\<version>\km\x64\ntoskrnl.lib` et s'arrête proprement si aucun n'est
utilisable.

## Build

```bat
driver\build.bat
```

Sortie : `build_driver\RrSvc.sys` (x64, sous-système NATIVE, entrée
`DriverEntry`). Le `.obj` intermédiaire est supprimé. `build_driver\` est
ignoré par Git.

Caractéristiques volontaires du binaire :

- imports **ntoskrnl uniquement** (`/NODEFAULTLIB`), aucun stub `FF 25` vers
  une autre DLL ;
- pas de CRT : `memcpy`, `memset`, `strlen` et `wcsncpy` sont des boucles
  maison, les structs sont initialisées champ par champ ;
- `/GS-` (pas de cookie, pas de `_GSHandlerCheckCommon`), `/GR-` ;
- en-têtes PE zéroés (0x400 octets via `DriverStart`) dès la fin de
  `DriverEntry`, plus aucune lecture des headers ensuite.

## Chargement

```bat
kdmapper.exe build_driver\RrSvc.sys
```

`DriverEntry` crée le device et le lien symbolique `\DosDevices\RrSvc94`.
L'overlay détecte le symlink au prochain `attach()` et bascule sur le driver ;
sinon il reste en RPM. Rien à configurer côté overlay.

Déchargement : `kdmapper.exe --unload RrSvc.sys` (le nettoyage des traces et la
suppression du device/lien se font dans `rr_unload`).

## Ordre de test

1. **Nettoyage désactivé d'abord.** Mettre `bool RR_CLEAN_TRACES = false;` dans
   `driver/Source.cpp`, recompiler, mapper. Valider le lien symbolique (`WinObj`
   ou un `CreateFileW` de test), puis une lecture simple (`CrashDump` / le client)
   et l'ESP. On isole ainsi les problèmes de device des problèmes de scrub.
2. **Nettoyage réactivé.** Remettre `RR_CLEAN_TRACES = true`, recompiler,
   remapper. Décharger et vérifier que `\Device\RrSvc94` a disparu, que
   `DriverUnload` est nul, et que les traces (`g_KernelHashBucketList`,
   `PiDDBCacheTable`, `MmUnloadedDrivers`) ne mentionnent plus `RrSvc`.
3. Test en jeu avec EAC, puis reboot pour repartir d'un noyau propre.

`RR_CLEAN_TRACES` est un booléen global : on peut le passer à `false` dans un
debugger sans recompiler le reste du driver.

## Vecteurs d'audit EAC et contre-mesures

| Observation côté EAC | Contre-mesure |
|---|---|
| Échantillonnage des piles noyau à 1 %, exige des adresses de retour dans un module connu | Driver 100 % ntoskrnl lié statiquement, aucune fonction d'un autre pilote dans la pile, aucun callback noyau |
| `IoEnumerateDeviceObjectList` + `ObQueryNameString` pour lister les devices | Nom de device non standard (`RrSvc94`), aucun objet nommé hors du device minimal, pas de `\FileSystem` |
| `MmCopyMemory` sur des pages pour signer le code | En-têtes PE zéroés, pas de CRT ni de marqueur de bibliothèque, code compact sans chaînes importées reconnaissables |
| Présence d'un minifilter (visibilité de l'I/O) | Aucun `FltRegisterFilter`, aucun rattachement au manager de filtres |
| Recherche de `_GSHandlerCheckCommon`, de `memset`/`memcpy` liés statiquement, de stubs d'import `FF 25` | `/GS-`, boucles maison, `/NODEFAULTLIB`, IAT limitée à ntoskrnl (le linker génère des appels directs, pas de thunks) |
| Croisement de `PoolBigPageTable` avec les plages de drivers | Staging de 64 Ko par `MmAllocateIndependentPages` (pages hors pool), donc hors gros pool ; repli pool tagué documenté |
| Blocage des NMI via `KiNmiInProgress` (ban rapide) | Aucun callback NMI, aucun DPC périodique, aucun thread système : le driver ne consomme pas de NMI |
| `PiDDBCacheTable` (chargement des `.sys`) | Scrub par `TimeDateStamp` : suppression de l'entrée (liste + AVL) au déchargement |
| `MmUnloadedDrivers` (anneau des déchargements) | Nom de l'entrée remis à zéro |
| `g_KernelHashBucketList` (images chargées par `RtlHashString`) | Entrée correspondante déliée et libérée |
| `DriverObject->DriverName` / `DriverUnload` visibles | `DriverName.Buffer` zéroé, `DriverUnload` mis à `nullptr` après le scrub |

Chaque étape de scrub est isolée dans son `__try/__except` et échoue
silencieusement : une signature `ntoskrnl` qui ne correspond plus ne casse jamais
le déchargement.

## Limites connues

- Les motifs de scan (`g_KernelHashBucketList`, `PiDDBCacheTable`,
  `MmUnloadedDrivers`) dépendent de la build de `ntoskrnl`. Ils sont regroupés en
  constantes commentées dans `Source.cpp` ; une nouvelle build majeure peut
  demander leur mise à jour. Le scrub PiDDB tente trois formes et valide les
  routines du `RTL_AVL_TABLE` avant d'écrire.
- `MmAllocateIndependentPages` n'est pas exportée par tous les noyaux (build
  26200 par exemple) : elle est résolue via `MmGetSystemRoutineAddress`, avec
  repli `ExAllocatePoolWithTag` si absente. Le repli est visible dans le pool.
- Le driver est mappé manuellement : il ne crée pas de service, n'a pas de clé
  de registre et n'apparaît pas dans `services.msc`.
- Mode « driver seul » (sans handle `OpenProcess`) : `regions()` et donc le
  scanner user-mode sont indisponibles, la validation d'adresse est approximative
  (adresse non nulle).
