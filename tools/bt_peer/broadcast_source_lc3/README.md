# JC Peer Cast - a real LC3 broadcast for testing Auracast listening

The ESP-IDF `bap/broadcast_source` example, changed to send real LC3 (a 440 Hz sine, 48 kHz, 10 ms, 100 B,
unencrypted) under the name "JC Peer Cast" instead of filler bytes. Flash it on the spare S31 board.
