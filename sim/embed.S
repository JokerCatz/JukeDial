/* The eight files the firmware embeds (EMBED_FILES in app/main/CMakeLists.txt), under the symbol names ESP-IDF gives them. */
#define EMBED(sym, path)                       \
    .section .rodata;                          \
    .global _binary_##sym##_start;             \
    .global _binary_##sym##_end;               \
    .balign 16;                                \
_binary_##sym##_start:                         \
    .incbin path;                              \
_binary_##sym##_end:                           \
    .byte 0

EMBED(alarm_mp3, "sounds/alarm.mp3")
EMBED(chime_mp3, "sounds/chime.mp3")
EMBED(slot_syms_bin, "assets/slot_syms.bin")
EMBED(lunch_zip, "assets/lists/lunch.zip")
EMBED(people_zip, "assets/lists/people.zip")
EMBED(ideas_zip, "assets/lists/ideas.zip")
EMBED(sample_zip, "assets/vocab/sample.zip")

.section .note.GNU-stack,"",@progbits
