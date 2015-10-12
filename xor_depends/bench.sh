#!/bin/sh
sh time_tool.sh R 16 -m TABLE -
sh time_tool.sh R 16 -m LOG -
sh time_tool.sh R 16 -m LOG -r CAUCHY -
sh time_tool.sh R 16 -m SPLIT 16 4 -
sh time_tool.sh R 16 -m SPLIT 16 4 -r ALTMAP -
sh time_tool.sh R 16 -m SPLIT 16 8 -
sh time_tool.sh R 16 -m SPLIT 8 8 -
sh time_tool.sh R 16 -m XOR_DEPENDS -
sh time_tool.sh R 16 -m XOR_DEPENDS -r ALTMAP -
