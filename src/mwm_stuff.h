/* MWM Motif stuff see also file Xm/MwmUtil.h */
typedef struct
{
  /* 32-bit property items are stored as long on the client (whether
   * that means 32 bits or 64).  XChangeProperty handles the conversion
   * to the actual 32-bit quantities sent to the server.
   */
  gulong flags;
  gulong functions;
  gulong decorations;
  glong  inputMode;
  gulong status;
} PropMwmHints;
/* Motif bits - see Motif reference manual for further information. */
#define MWM_HINTS_FUNCTIONS     (1L << 0)
#define MWM_HINTS_DECORATIONS   (1L << 1)
#define MWM_HINTS_INPUT_MODE    (1L << 2)
#define MWM_HINTS_STATUS        (1L << 3)
    
#define MWM_DECOR_ALL           (1L << 0)
#define MWM_DECOR_BORDER        (1L << 1)
#define MWM_DECOR_RESIZEH       (1L << 2)
#define MWM_DECOR_TITLE         (1L << 3)
#define MWM_DECOR_MENU          (1L << 4)
#define MWM_DECOR_MINIMIZE      (1L << 5)
#define MWM_DECOR_MAXIMIZE      (1L << 6)
/* number of elements of size 32 in _MWM_HINTS */
#define PROP_MOTIF_WM_HINTS_ELEMENTS  5

