PlayStation button prompts - Skate 3 for PS5
=============================================

Skate 3 draws each button prompt from a small picture of its own (64x64,
the button in the top-left 48x48). playstation/ holds a PlayStation version
of every Xbox picture the port knows, named after the Xbox picture it
replaces:

    playstation/<id>.dds     <id> = the Xbox picture's FNV-1a 64 hash
                             (DXT5, 64x64, full mip chain)

The hub's Button Prompts setting (GAME) picks PlayStation (these) or Xbox
(the game's own). Nothing in the game's files changes: the port swaps a
picture in memory when the game loads it, for both renderers.

Replaced so far (32 pictures)
-----------------------------
    A -> Cross (x2)       LB -> L1 (x2)     LS / RS -> L / R sticks, and with
    B -> Circle (x2)      RB -> R1 (x2)       their up / down / left arrows
    X -> Square           LT -> L2 (x2)     LS / RS click -> L3 / R3 (x3)
    Y -> Triangle         RT -> R2 (x2)     Start -> OPTIONS
                                            D-pad (7 pictures)
(x2: the game draws some buttons in two styles - with and without the black
outline.) Drawn, waiting for their Xbox picture: CREATE (Back), the sticks'
other arrows, outlined Square / Triangle.

Changing a button
-----------------
Drop your own <id>.dds in playstation/ (DXT5, 64x64, with mips; the button
in the top-left 48x48) over the one you want to change.

A new replacement: set skate3_ps5_dump_hud_pictures = true in
UserData/settings.toml and visit the screen; logs/hud_pictures gets every
menu/HUD picture as hud_<w>x<h>_<format>_s<swizzle>_<id>.dds. Draw the
PlayStation version at the same size and format and save it as
playstation/<id>.dds.
