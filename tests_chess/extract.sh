#!/bin/sh
# Ambil blok ENGINE + AI dari chess_renphone.ino -> engine_ai.inc (dipakai test_chess.cpp)
sed -n '/==== ENGINE BEGIN ====/,/==== ENGINE END ====/p;/==== AI BEGIN ====/,/==== AI END ====/p' ../chess_renphone.ino > engine_ai.inc
