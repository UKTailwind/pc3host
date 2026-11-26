' The VGA and HDMI PicoMite's framebuffers on a PC: MERGE puts
' framebuffer 2 over F, and the layers L and T are shown live over the
' screen, T over L, each skipping its own transparent index.  run.sh
' reads the pixels of the window pc3d presented as the program ended
' (pc3d --snapshot), which is the only place the layers are to be seen:
' PIXEL() and SAVE IMAGE read the screen, N, as on the firmware.
MODE 2
FRAMEBUFFER CREATE
FRAMEBUFFER CREATE 2
FRAMEBUFFER WRITE F : CLS RGB(BLUE) : BOX 0, 200, 320, 40, 0, RGB(GREEN), RGB(GREEN)
FRAMEBUFFER WRITE 2 : CLS RGB(255, 0, 255)     ' index 9: MERGE's transparent here
BOX 250, 10, 40, 40, 0, RGB(WHITE), RGB(WHITE)
FRAMEBUFFER MERGE 9                             ' 2 over F onto N
FRAMEBUFFER LAYER 9
FRAMEBUFFER WRITE L : BOX 40, 40, 160, 120, 0, RGB(YELLOW), RGB(YELLOW)
FRAMEBUFFER LAYER TOP 3
FRAMEBUFFER WRITE T : CIRCLE 200, 120, 50, 0, 1, RGB(RED), RGB(RED)
FRAMEBUFFER WRITE N
