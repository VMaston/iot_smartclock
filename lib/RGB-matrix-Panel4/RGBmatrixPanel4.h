#if ARDUINO >= 100
 #include "Arduino.h"
#else
 #include "WProgram.h"
 #include "pins_arduino.h"
#endif
#include "Adafruit_GFX.h"

#if defined(__AVR__)
  typedef uint8_t  PortType;
#elif defined(__arm__) || defined(__xtensa__)
  typedef uint32_t PortType; // Formerly 'RwReg' but interfered w/CMCIS header
#endif

class RGBmatrixPanel4 : public Adafruit_GFX {

 public:

  // Constructor for 16x32 panel:
  RGBmatrixPanel4(uint8_t a, uint8_t b, uint8_t c,
    uint8_t sclk, uint8_t latch, uint8_t oe, boolean dbuf, uint8_t pwidth
#if defined(ARDUINO_ARCH_SAMD) || defined(ARDUINO_ARCH_ESP32)
    ,uint8_t *pinlist=NULL
#endif
    );
    /* Parameters
    a, b, c are the pins used for addressing the rows
    cclk, latch and oe are the pins used for Serial Clock, Latach and Output Enable
    dbuf enables double buffering. This will use 2x RAM for frame buffer, but will give nice smooth animation
    pwidth is the number of Panels used together in a multi panel configuration
    */

  // Constructor for 32x32 panel (adds 'd' pin): (THIS HAS NOT BEEN TESTED WITH MULTIPLE PANELS)
  RGBmatrixPanel4(uint8_t a, uint8_t b, uint8_t c, uint8_t d,
    uint8_t sclk, uint8_t latch, uint8_t oe, boolean dbuf,uint8_t pwidth
#if defined(ARDUINO_ARCH_SAMD) || defined(ARDUINO_ARCH_ESP32)
    ,uint8_t *pinlist=NULL
#endif
    );

  void
    begin(void),
    drawPixel(int16_t x, int16_t y, uint16_t c),
    fillScreen(uint16_t c),
    updateDisplay(void),
    swapBuffers(boolean),
    dumpMatrix(void),
	getPtrAddress(void);
  uint8_t
    *backBuffer(void);
  uint16_t
    Color333(uint8_t r, uint8_t g, uint8_t b),
    Color444(uint8_t r, uint8_t g, uint8_t b),
    Color888(uint8_t r, uint8_t g, uint8_t b),
    Color888(uint8_t r, uint8_t g, uint8_t b, boolean gflag),
    ColorHSV(long hue, uint8_t sat, uint8_t val, boolean gflag);

  // Printing
 private:

  uint8_t *matrixbuff[2];
  uint8_t nRows, nPlanes, backindex, nPanels, nMultiplexRows, nCounter;
  boolean swapflag, written;
    
  // Init/alloc code common to both constructors:
  void init(uint8_t rows, uint8_t a, uint8_t b, uint8_t c,
    uint8_t sclk, uint8_t latch, uint8_t oe, boolean dbuf, uint8_t pwidth
#if defined(ARDUINO_ARCH_SAMD) || defined(ARDUINO_ARCH_ESP32)
            ,uint8_t *rgbpins
#endif
    );

  // PORT register pointers, pin bitmasks, pin numbers:
  volatile uint32_t
    *latport, *oeport, *addraport, *addrbport, *addrcport, *addrdport;
  uint32_t
    sclkpin, latpin, oepin, addrapin, addrbpin, addrcpin, addrdpin,
    _sclk, _latch, _oe, _a, _b, _c, _d;
  PortType clkmask, latmask, oemask,        // Pin bitmasks
           addramask, addrbmask, addrcmask, addrdmask; 
  // PORT register pointers (CLKPORT is hardcoded on AVR)
//  volatile PortType *latport, *oeport,
//                    *addraport, *addrbport, *addrcport, *addrdport;

#if defined(ARDUINO_ARCH_SAMD) || defined(ARDUINO_ARCH_ESP32)
  uint8_t  rgbpins[6];                      // Pin numbers for 2x R,G,B bits
  volatile PortType *outsetreg, *outclrreg; // PORT bit set, clear registers
  PortType           rgbclkmask;            // Mask of all RGB bits + CLK
  PortType           expand[256];           // 6-to-32 bit converter table
#endif

  // Counters/pointers for interrupt handler:
  volatile uint8_t row, plane;
  volatile uint8_t *buffptr;
};

