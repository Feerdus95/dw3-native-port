# Launcher translations to review

The launcher's English text is the source. The other four languages were written without native speakers and have
**not been checked by native speakers**. This lists the strings added with the home screen, the team cards and the
Saves page. To correct one, edit the matching `[S_...]` line in `launcher/lang_<code>.c` (or just say what it should be).

Notes for reviewers:

- `%d` is a number the launcher fills in; keep it. The text must stay short: it is drawn in a pixel font on a 480x270 screen,
  and `dw3launcher.exe --check` fails when a line doesn't fit, so a longer version may need shortening.
- "Slot" is the memory card's save slot (kept as in the game). "Extra Digimon" are the ones the port adds beyond the game's 44.
- "saves/backups" is a folder name and stays as it is.

## Español

| String | English | Español | Correction |
|---|---|---|---|
| `S_TEAM` | Your team | Tu equipo | |
| `S_LAST_SAVE` | Last save: | Último save: | |
| `S_LEVEL` | Lv. | Nv. | |
| `S_NO_TEAM` | No save with a team yet. Save in the game and it shows up here. | Aún no hay un save con equipo. Guarda en el juego y aparecerá aquí. | |
| `S_SAVES` | Saves | Partidas | |
| `S_SAVES_D` | Your memory card: export it as a backup, import another, or go back to the last copy. Every change keeps the card you had. | Tu memory card: expórtala como copia, importa otra o vuelve a la última copia. Cada cambio conserva la tarjeta que tenías. | |
| `S_EXPORT` | Export card | Exportar tarjeta | |
| `S_EXPORT_D` | Copies your memory card to a file you choose: a backup, or to play on another computer. | Copia tu memory card a un archivo que elijas: una copia de seguridad, o para jugar en otro equipo. | |
| `S_IMPORT` | Import card | Importar tarjeta | |
| `S_IMPORT_D` | Replaces your card with one from a file (a PlayStation or emulator .mcd works). The card you have now is kept in saves/backups first. The port's extra Digimon only come along if the file has them. | Reemplaza tu tarjeta por la de un archivo (sirve un .mcd de PlayStation o de emulador). Antes se guarda la actual en saves/backups. Los Digimon extra del port solo vienen si el archivo los tiene. | |
| `S_UNDO` | Restore last copy | Restaurar última copia | |
| `S_UNDO_D` | Goes back to the newest copy kept in saves/backups. The card you have now is kept too. | Vuelve a la copia más reciente de saves/backups. También se guarda la tarjeta actual. | |
| `S_SV_FOLDER` | Open saves folder | Abrir carpeta de saves | |
| `S_SV_FOLDER_D` | Opens the folder with your memory card and its copies. | Abre la carpeta con tu memory card y sus copias. | |
| `S_SV_SLOT` | Slot | Slot | |
| `S_SV_EMPTY` | empty | vacío | |
| `S_SV_DAMAGED` | damaged | dañado | |
| `S_SV_NONE` | no extra Digimon | sin Digimon extra | |
| `S_SV_OK` | extra Digimon: %d | Digimon extra: %d | |
| `S_SV_STALE` | extra Digimon not used: saved elsewhere since | Digimon extra sin usar: se guardó en otro lado después | |
| `S_SV_NEWER` | extra Digimon from a newer version, kept | Digimon extra de una versión más nueva, se conservan | |
| `S_SV_BAD` | extra Digimon damaged | Digimon extra dañados | |
| `S_SV_NOCARD` | No memory card yet. The game makes it when you save. | Aún no hay memory card. El juego la crea al guardar. | |
| `S_SV_EXPORTED` | Card exported. | Tarjeta exportada. | |
| `S_SV_IMPORTED` | Card imported. The previous one is in saves/backups. | Tarjeta importada. La anterior está en saves/backups. | |
| `S_SV_WARN` | Card imported. Some slots were saved elsewhere after their extra Digimon, so those aren't used. The previous card is in saves/backups. | Tarjeta importada. Algunos slots se guardaron en otro lado después de sus Digimon extra, y no se usan. La anterior está en saves/backups. | |
| `S_SV_REJECT` | That isn't a 128 KB memory card with a Digimon World 2003 save. | Ese archivo no es una memory card de 128 KB con un save de Digimon World 2003. | |
| `S_SV_RESTORED` | Restored. The card you had is in saves/backups. | Restaurada. La tarjeta que tenías está en saves/backups. | |
| `S_SV_NO_BACKUP` | No copy to go back to yet. | Aún no hay copia a la que volver. | |
| `S_SV_FAILED` | The file couldn't be copied. Nothing changed. | No se pudo copiar el archivo. Nada cambió. | |

## Français

| String | English | Français | Correction |
|---|---|---|---|
| `S_TEAM` | Your team | Votre équipe | |
| `S_LAST_SAVE` | Last save: | Dernière sauvegarde : | |
| `S_LEVEL` | Lv. | Niv. | |
| `S_NO_TEAM` | No save with a team yet. Save in the game and it shows up here. | Pas encore de sauvegarde avec équipe. Sauvegardez dans le jeu. | |
| `S_SAVES` | Saves | Parties | |
| `S_SAVES_D` | Your memory card: export it as a backup, import another, or go back to the last copy. Every change keeps the card you had. | Votre carte mémoire : exportez-la, importez-en une autre ou revenez à la dernière copie. | |
| `S_EXPORT` | Export card | Exporter la carte | |
| `S_EXPORT_D` | Copies your memory card to a file you choose: a backup, or to play on another computer. | Copie votre carte mémoire dans un fichier de votre choix : une sauvegarde, ou pour jouer sur un autre ordinateur. | |
| `S_IMPORT` | Import card | Importer une carte | |
| `S_IMPORT_D` | Replaces your card with one from a file (a PlayStation or emulator .mcd works). The card you have now is kept in saves/backups first. The port's extra Digimon only come along if the file has them. | Remplace votre carte par celle d'un fichier (un .mcd de PlayStation ou d'émulateur convient). La carte actuelle est d'abord gardée dans saves/backups. Les Digimon en plus du port ne viennent que si le fichier les a. | |
| `S_UNDO` | Restore last copy | Revenir à la copie | |
| `S_UNDO_D` | Goes back to the newest copy kept in saves/backups. The card you have now is kept too. | Revient à la copie la plus récente de saves/backups. La carte actuelle est gardée aussi. | |
| `S_SV_FOLDER` | Open saves folder | Ouvrir les saves | |
| `S_SV_FOLDER_D` | Opens the folder with your memory card and its copies. | Ouvre le dossier de votre carte mémoire et de ses copies. | |
| `S_SV_SLOT` | Slot | Slot | |
| `S_SV_EMPTY` | empty | vide | |
| `S_SV_DAMAGED` | damaged | abîmé | |
| `S_SV_NONE` | no extra Digimon | pas de Digimon en plus | |
| `S_SV_OK` | extra Digimon: %d | Digimon en plus : %d | |
| `S_SV_STALE` | extra Digimon not used: saved elsewhere since | Digimon en plus ignorés : autre sauvegarde depuis | |
| `S_SV_NEWER` | extra Digimon from a newer version, kept | Digimon en plus d'une version récente, gardés | |
| `S_SV_BAD` | extra Digimon damaged | Digimon en plus abîmés | |
| `S_SV_NOCARD` | No memory card yet. The game makes it when you save. | Pas encore de carte. Le jeu la crée à la sauvegarde. | |
| `S_SV_EXPORTED` | Card exported. | Carte exportée. | |
| `S_SV_IMPORTED` | Card imported. The previous one is in saves/backups. | Carte importée. L'ancienne est dans saves/backups. | |
| `S_SV_WARN` | Card imported. Some slots were saved elsewhere after their extra Digimon, so those aren't used. The previous card is in saves/backups. | Carte importée. Des slots ont été sauvegardés ailleurs après leurs Digimon en plus, qui ne sont donc pas utilisés. L'ancienne carte est dans saves/backups. | |
| `S_SV_REJECT` | That isn't a 128 KB memory card with a Digimon World 2003 save. | Ce n'est pas une carte mémoire de 128 Ko avec une sauvegarde de Digimon World 2003. | |
| `S_SV_RESTORED` | Restored. The card you had is in saves/backups. | Restaurée. La carte que vous aviez est dans saves/backups. | |
| `S_SV_NO_BACKUP` | No copy to go back to yet. | Pas encore de copie où revenir. | |
| `S_SV_FAILED` | The file couldn't be copied. Nothing changed. | Le fichier n'a pas pu être copié. Rien n'a changé. | |

## Deutsch

| String | English | Deutsch | Correction |
|---|---|---|---|
| `S_TEAM` | Your team | Dein Team | |
| `S_LAST_SAVE` | Last save: | Letzter Spielstand: | |
| `S_LEVEL` | Lv. | St. | |
| `S_NO_TEAM` | No save with a team yet. Save in the game and it shows up here. | Noch kein Spielstand mit Team. Speichere im Spiel, dann erscheint er hier. | |
| `S_SAVES` | Saves | Spielstände | |
| `S_SAVES_D` | Your memory card: export it as a backup, import another, or go back to the last copy. Every change keeps the card you had. | Deine Memory Card: exportieren, eine andere importieren oder zur letzten Kopie zurück. | |
| `S_EXPORT` | Export card | Karte exportieren | |
| `S_EXPORT_D` | Copies your memory card to a file you choose: a backup, or to play on another computer. | Kopiert deine Memory Card in eine Datei deiner Wahl: eine Sicherung, oder zum Spielen auf einem anderen Computer. | |
| `S_IMPORT` | Import card | Karte importieren | |
| `S_IMPORT_D` | Replaces your card with one from a file (a PlayStation or emulator .mcd works). The card you have now is kept in saves/backups first. The port's extra Digimon only come along if the file has them. | Ersetzt deine Karte durch die aus einer Datei (eine .mcd von PlayStation oder Emulator geht). Die aktuelle wird vorher in saves/backups gesichert. Die Extra-Digimon des Ports kommen nur mit, wenn die Datei sie hat. | |
| `S_UNDO` | Restore last copy | Letzte Kopie laden | |
| `S_UNDO_D` | Goes back to the newest copy kept in saves/backups. The card you have now is kept too. | Geht zur neuesten Kopie in saves/backups zurück. Die aktuelle Karte wird auch gesichert. | |
| `S_SV_FOLDER` | Open saves folder | Spielstand-Ordner öffnen | |
| `S_SV_FOLDER_D` | Opens the folder with your memory card and its copies. | Öffnet den Ordner mit deiner Memory Card und ihren Kopien. | |
| `S_SV_SLOT` | Slot | Slot | |
| `S_SV_EMPTY` | empty | leer | |
| `S_SV_DAMAGED` | damaged | beschädigt | |
| `S_SV_NONE` | no extra Digimon | keine Extra-Digimon | |
| `S_SV_OK` | extra Digimon: %d | Extra-Digimon: %d | |
| `S_SV_STALE` | extra Digimon not used: saved elsewhere since | Extra-Digimon ungenutzt: seither anderswo gespeichert | |
| `S_SV_NEWER` | extra Digimon from a newer version, kept | Extra-Digimon einer neueren Version, bleiben erhalten | |
| `S_SV_BAD` | extra Digimon damaged | Extra-Digimon beschädigt | |
| `S_SV_NOCARD` | No memory card yet. The game makes it when you save. | Noch keine Memory Card. Das Spiel legt sie beim Speichern an. | |
| `S_SV_EXPORTED` | Card exported. | Karte exportiert. | |
| `S_SV_IMPORTED` | Card imported. The previous one is in saves/backups. | Karte importiert. Die vorherige liegt in saves/backups. | |
| `S_SV_WARN` | Card imported. Some slots were saved elsewhere after their extra Digimon, so those aren't used. The previous card is in saves/backups. | Karte importiert. Manche Slots wurden nach ihren Extra-Digimon anderswo gespeichert, diese werden nicht genutzt. Die vorherige Karte liegt in saves/backups. | |
| `S_SV_REJECT` | That isn't a 128 KB memory card with a Digimon World 2003 save. | Das ist keine 128-KB-Memory-Card mit einem Digimon-World-2003-Spielstand. | |
| `S_SV_RESTORED` | Restored. The card you had is in saves/backups. | Wiederhergestellt. Deine bisherige Karte liegt in saves/backups. | |
| `S_SV_NO_BACKUP` | No copy to go back to yet. | Noch keine Kopie zum Zurückgehen. | |
| `S_SV_FAILED` | The file couldn't be copied. Nothing changed. | Die Datei ließ sich nicht kopieren. Nichts wurde geändert. | |

## Italiano

| String | English | Italiano | Correction |
|---|---|---|---|
| `S_TEAM` | Your team | La tua squadra | |
| `S_LAST_SAVE` | Last save: | Ultimo salvataggio: | |
| `S_LEVEL` | Lv. | Liv. | |
| `S_NO_TEAM` | No save with a team yet. Save in the game and it shows up here. | Nessun salvataggio con squadra. Salva nel gioco e apparirà qui. | |
| `S_SAVES` | Saves | Salvataggi | |
| `S_SAVES_D` | Your memory card: export it as a backup, import another, or go back to the last copy. Every change keeps the card you had. | La tua memory card: esportala come copia, importane un'altra o torna all'ultima copia. Ogni modifica conserva la scheda che avevi. | |
| `S_EXPORT` | Export card | Esporta scheda | |
| `S_EXPORT_D` | Copies your memory card to a file you choose: a backup, or to play on another computer. | Copia la tua memory card in un file a tua scelta: una copia di sicurezza, o per giocare su un altro computer. | |
| `S_IMPORT` | Import card | Importa scheda | |
| `S_IMPORT_D` | Replaces your card with one from a file (a PlayStation or emulator .mcd works). The card you have now is kept in saves/backups first. The port's extra Digimon only come along if the file has them. | Sostituisce la tua scheda con quella di un file (va bene un .mcd di PlayStation o di emulatore). Prima la scheda attuale viene salvata in saves/backups. I Digimon extra del port arrivano solo se il file li ha. | |
| `S_UNDO` | Restore last copy | Ripristina ultima copia | |
| `S_UNDO_D` | Goes back to the newest copy kept in saves/backups. The card you have now is kept too. | Torna alla copia più recente in saves/backups. Anche la scheda attuale viene conservata. | |
| `S_SV_FOLDER` | Open saves folder | Apri salvataggi | |
| `S_SV_FOLDER_D` | Opens the folder with your memory card and its copies. | Apre la cartella con la tua memory card e le sue copie. | |
| `S_SV_SLOT` | Slot | Slot | |
| `S_SV_EMPTY` | empty | vuoto | |
| `S_SV_DAMAGED` | damaged | danneggiato | |
| `S_SV_NONE` | no extra Digimon | nessun Digimon extra | |
| `S_SV_OK` | extra Digimon: %d | Digimon extra: %d | |
| `S_SV_STALE` | extra Digimon not used: saved elsewhere since | Digimon extra non usati: salvato altrove da allora | |
| `S_SV_NEWER` | extra Digimon from a newer version, kept | Digimon extra di una versione più recente, conservati | |
| `S_SV_BAD` | extra Digimon damaged | Digimon extra danneggiati | |
| `S_SV_NOCARD` | No memory card yet. The game makes it when you save. | Ancora nessuna memory card. Il gioco la crea quando salvi. | |
| `S_SV_EXPORTED` | Card exported. | Scheda esportata. | |
| `S_SV_IMPORTED` | Card imported. The previous one is in saves/backups. | Scheda importata. La precedente è in saves/backups. | |
| `S_SV_WARN` | Card imported. Some slots were saved elsewhere after their extra Digimon, so those aren't used. The previous card is in saves/backups. | Scheda importata. Alcuni slot sono stati salvati altrove dopo i loro Digimon extra, che quindi non si usano. La precedente è in saves/backups. | |
| `S_SV_REJECT` | That isn't a 128 KB memory card with a Digimon World 2003 save. | Non è una memory card da 128 KB con un salvataggio di Digimon World 2003. | |
| `S_SV_RESTORED` | Restored. The card you had is in saves/backups. | Ripristinata. La scheda che avevi è in saves/backups. | |
| `S_SV_NO_BACKUP` | No copy to go back to yet. | Ancora nessuna copia a cui tornare. | |
| `S_SV_FAILED` | The file couldn't be copied. Nothing changed. | Impossibile copiare il file. Non è cambiato nulla. | |
