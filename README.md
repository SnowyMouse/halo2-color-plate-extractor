# Halo 2 Color Plate Extractor

This tool extracts color plates from a bitmap tag.

You will need to have the H2EK (Halo 2 Mod Tools) installed. You can get this
from Steam at https://store.steampowered.com/app/1613450/Halo_2_Mod_Tools__MCC/

The usage for this tool is:
`halo2-color-plate-extractor <tags> <data> <tag-path|"all"|"all-overwrite">`

* `<tags-dir>` is the path to your tags directory.
* `<data-dir>` is the path to your data directory.
* `<tag-path|"all"|"all-overwrite">` can be either the path to the tag, "all" to recursively extract all tags, and "all-overwrite" to extract all tags even if a .tif exists

If you want to compile this, you will need the following:
* C++20 compiler such as GCC (G++)
* LibTIFF
