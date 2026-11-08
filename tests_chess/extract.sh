#!/bin/sh
# Ambil blok ENGINE + AI dari chess_renphone.ino -> engine_ai.inc (dipakai test_chess.cpp),
# dan blok GEM (adu mesin vs Gemini) -> gem_block.inc (dipakai test_gem.cpp)
sed -n '/==== ENGINE BEGIN ====/,/==== ENGINE END ====/p;/==== AI BEGIN ====/,/==== AI END ====/p' ../chess_renphone.ino > engine_ai.inc
sed -n '/==== GEM BEGIN ====/,/==== GEM END ====/p' ../chess_renphone.ino > gem_block.inc
