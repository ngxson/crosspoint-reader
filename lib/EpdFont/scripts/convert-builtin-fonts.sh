#!/bin/bash

set -e

cd "$(dirname "$0")"

READER_FONT_STYLES=("Regular" "Italic" "Bold" "BoldItalic")
BOOKERLY_FONT_SIZES=(12 14 16 18)
NOTOSANS_FONT_SIZES=(12 14 16 18)
OPENDYSLEXIC_FONT_SIZES=(8 10 12 14)
ASSETS_PATH="../builtinFonts/assets.bin"

# Create an empty assets file with 4 leading bytes
echo -n "ABCD" > $ASSETS_PATH

for size in ${BOOKERLY_FONT_SIZES[@]}; do
  for style in ${READER_FONT_STYLES[@]}; do
    font_name="bookerly_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')"
    font_path="../builtinFonts/source/Bookerly/Bookerly-${style}.ttf"
    output_path="../builtinFonts/${font_name}.h"
    python fontconvert.py $font_name $size $font_path --2bit --assets-path $ASSETS_PATH > $output_path
    echo "Generated $output_path"
  done
done

for size in ${NOTOSANS_FONT_SIZES[@]}; do
  for style in ${READER_FONT_STYLES[@]}; do
    font_name="notosans_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')"
    font_path="../builtinFonts/source/NotoSans/NotoSans-${style}.ttf"
    output_path="../builtinFonts/${font_name}.h"
    python fontconvert.py $font_name $size $font_path --2bit --assets-path $ASSETS_PATH > $output_path
    echo "Generated $output_path"
  done
done

for size in ${OPENDYSLEXIC_FONT_SIZES[@]}; do
  for style in ${READER_FONT_STYLES[@]}; do
    font_name="opendyslexic_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')"
    font_path="../builtinFonts/source/OpenDyslexic/OpenDyslexic-${style}.otf"
    output_path="../builtinFonts/${font_name}.h"
    python fontconvert.py $font_name $size $font_path --2bit --assets-path $ASSETS_PATH > $output_path
    echo "Generated $output_path"
  done
done

# CJK Fonts
READER_FONT_STYLES=("Regular" "Bold")
FONT_ARGS="--no-default-intervals"
FONT_ARGS="$FONT_ARGS --additional-intervals 0x4E00,0x9FFF" # Core Unified Ideographs
FONT_ARGS="$FONT_ARGS --additional-intervals 0x3040,0x309F" # Hiragana
FONT_ARGS="$FONT_ARGS --additional-intervals 0x30A0,0x30FF" # Katakana

NOTOSERIFSC_FONT_SIZES=(12)

# for size in ${NOTOSANS_FONT_SIZES[@]}; do
#   for style in ${READER_FONT_STYLES[@]}; do
#     font_name="notosanssc_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')"
#     font_path="../builtinFonts/source/NotoSansSC/NotoSansSC-${style}.ttf"
#     output_path="../builtinFonts/${font_name}.h"
#     python fontconvert.py $font_name $size $font_path --2bit $FONT_ARGS --assets-path $ASSETS_PATH > $output_path
#     echo "Generated $output_path"
#   done
# done

for size in ${NOTOSERIFSC_FONT_SIZES[@]}; do
  for style in ${READER_FONT_STYLES[@]}; do
    font_name="notoserifsc_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')"
    font_path="../builtinFonts/source/NotoSerifSC/NotoSerifSC-${style}.ttf"
    output_path="../builtinFonts/${font_name}.h"
    python fontconvert.py $font_name $size $font_path --2bit $FONT_ARGS --assets-path $ASSETS_PATH > $output_path
    echo "Generated $output_path"
  done
done

UI_FONT_SIZES=(10 12)
UI_FONT_STYLES=("Regular" "Bold")

for size in ${UI_FONT_SIZES[@]}; do
  for style in ${UI_FONT_STYLES[@]}; do
    font_name="ubuntu_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')"
    font_path="../builtinFonts/source/Ubuntu/Ubuntu-${style}.ttf"
    output_path="../builtinFonts/${font_name}.h"
    python fontconvert.py $font_name $size $font_path > $output_path
    echo "Generated $output_path"
  done
done

python fontconvert.py notosans_8_regular 8 ../builtinFonts/source/NotoSans/NotoSans-Regular.ttf > ../builtinFonts/notosans_8_regular.h
