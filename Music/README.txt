NFL 2K5 Local Music
===================

Put music you have permission to use in this folder. Supported local formats
are MP3, M4A/AAC, FLAC, OGG, WAV and WMA. Each folder (and sub-folder) is a
playlist; songs straight in this folder play as "Music".

The installer does not distribute commercial music or broadcast themes.

Music Player: Options > Music Player in the game's menus (F1 > Music Player on
PC too) lists every song of a playlist. Scroll with Up/Down (hold to go
faster), jump a page with LB/RB, change the playlist with Left/Right or LT/RT,
and press A to play a song. R3 skips to the next song, L3 shows what is playing.

Song titles
-----------
The list shows short titles ("Song Titles: Short", the default):
  1. your own, from titles.txt in this folder (see below), else
  2. the song's title tag (ID3 in MP3, Vorbis comments in OGG/FLAC/Opus, the
     iTunes title in M4A, INFO in WAV), else
  3. the file name, cleaned up: underscores and repeated spaces go; a leading
     track number or year ("01 - ", "1-03 ", "1982 ") goes; "(Official Video)",
     "[Lyrics]", "(Remastered 2011)" and the like go; the artist or album is
     taken off when the folder name or the other songs' tags name it (so
     "Rush - Signals982 Subdivisions Rush-Signals.mp3" shows "Subdivisions").
     When it is not sure, it keeps the longer name.
Set Song Titles to "Full File Name" in the Music Player to see the file names.

titles.txt: one line per song, the file name (with or without .mp3), "=", and
the title you want. Lines starting with # are notes. Example:

  # my titles
  1982 Subdivisions Rush-Signals = Subdivisions
  Rock/Live/06 - Nested.mp3 = Live Track
