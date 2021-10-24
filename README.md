# Halo 2 Color Plate Extractor

This tool extracts color plates from a Halo 2 bitmap tag.

You will need to have a version of the H2EK to get a tagset.

* For the Steam version, you can download the Halo 2 Mod Tools at
  https://store.steampowered.com/app/1613450/Halo_2_Mod_Tools__MCC/

* For the Windows store version, you can buy the Steam version at 
  https://store.steampowered.com/app/1064270/Halo_2_Anniversary/ and
  then download the MCC Halo 2 Mod Tools at
  https://store.steampowered.com/app/1613450/Halo_2_Mod_Tools__MCC/

* For Halo 2 Vista, the H2EK can be installed from the disc that came with the
  game.

The usage for this tool is:

```
halo2-color-plate-extractor <tags-dir> <data-dir> <tag-path|"all"|"all-overwrite">
```

* `<tags-dir>` is the path to your tags directory.
* `<data-dir>` is the path to your data directory.
* `<tag-path|"all"|"all-overwrite">` can be either the path to the tag, "all" to recursively extract all tags, and "all-overwrite" to extract all tags even if a .tif exists

If you want to compile this, you will need the following:
* C++20 compiler such as GCC (G++)
* LibTIFF
