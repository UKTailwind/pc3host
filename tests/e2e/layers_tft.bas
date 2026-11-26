' MMBasic's TFT model, which every PC3 program used before framebuffer 2:
' with no 2, MERGE puts the layer over F, and from that MERGE on the
' layer is merged and no longer shown live - so a sprite moved in the
' layer is not seen twice, once live and once in the merged picture.
' The white box drawn into the layer after the MERGE must not be in the
' window run.sh reads.
MODE 2
FRAMEBUFFER CREATE
FRAMEBUFFER LAYER 9
FRAMEBUFFER WRITE F : CLS RGB(BLUE)
FRAMEBUFFER WRITE L : BOX 40, 40, 160, 120, 0, RGB(YELLOW), RGB(YELLOW)
FRAMEBUFFER WRITE N : CLS RGB(BLACK)
FRAMEBUFFER MERGE                               ' L over F, the layer's own 9
FRAMEBUFFER WRITE L : CLS RGB(255, 0, 255) : BOX 0, 0, 20, 20, 0, RGB(WHITE), RGB(WHITE)
FRAMEBUFFER WRITE N
