#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>


// ============================================================
//                    CONFIGURATION
// ============================================================

// ---------------- WiFi AP ----------------

const char* AP_SSID     = "ESP32-C3-PWM";
const char* AP_PASSWORD = "12345678";


// ---------------- OLED ----------------

#define OLED_SDA      6
#define OLED_SCL      7
#define OLED_ADDRESS  0x3C

#define OLED_WIDTH    128
#define OLED_HEIGHT   64


// ---------------- PWM ----------------

#define PWM_PIN        10
#define PWM_FREQUENCY  5000
#define PWM_RESOLUTION 12

#define PWM_MAX        4095


// ============================================================
//                    NETWORK
// ============================================================

WiFiServer server(80);

WiFiClient wsClient;

bool websocketConnected = false;


// HTTP request buffer

char httpBuffer[1200];
size_t httpLength = 0;


// WebSocket receive buffer

uint8_t wsBuffer[256];
size_t wsLength = 0;


// ============================================================
//                    PWM
// ============================================================

uint16_t duty = 0;


// ============================================================
//                    OLED BUFFER
// ============================================================

uint8_t oledBuffer[
  OLED_WIDTH * OLED_HEIGHT / 8
];


// ============================================================
//                    OLED LOW LEVEL
// ============================================================

void oledCommand(uint8_t command)
{
  Wire.beginTransmission(OLED_ADDRESS);

  Wire.write(0x00);
  Wire.write(command);

  Wire.endTransmission();
}


void oledData(
  uint8_t* data,
  uint16_t length
)
{
  while (length > 0)
  {
    uint8_t chunk =
      min((uint16_t)16, length);


    Wire.beginTransmission(
      OLED_ADDRESS
    );

    Wire.write(0x40);


    for (uint8_t i = 0; i < chunk; i++)
    {
      Wire.write(data[i]);
    }


    Wire.endTransmission();


    data += chunk;
    length -= chunk;
  }
}


// ============================================================
//                    OLED INIT
// ============================================================

void oledInit()
{
  Wire.begin(
    OLED_SDA,
    OLED_SCL
  );

  Wire.setClock(400000);

  delay(100);


  oledCommand(0xAE); // Display OFF

  oledCommand(0xD5);
  oledCommand(0x80);

  oledCommand(0xA8);
  oledCommand(0x3F);

  oledCommand(0xD3);
  oledCommand(0x00);

  oledCommand(0x40);

  oledCommand(0x8D);
  oledCommand(0x14);

  oledCommand(0xA1);

  oledCommand(0xC8);

  oledCommand(0xDA);
  oledCommand(0x12);

  oledCommand(0x81);
  oledCommand(0x7F);

  oledCommand(0xD9);
  oledCommand(0xF1);

  oledCommand(0xDB);
  oledCommand(0x40);

  oledCommand(0xA4);

  oledCommand(0xA6);

  oledCommand(0xAF); // Display ON


  memset(
    oledBuffer,
    0,
    sizeof(oledBuffer)
  );
}


// ============================================================
//                    OLED DISPLAY
// ============================================================

void oledDisplay()
{
  for (uint8_t page = 0; page < 8; page++)
  {
    oledCommand(
      0xB0 + page
    );

    // SH1106 column offset
    oledCommand(0x02);
    oledCommand(0x10);


    oledData(
      &oledBuffer[
        page * 128
      ],
      128
    );
  }
}


// ============================================================
//                    OLED GRAPHICS
// ============================================================

void oledClear()
{
  memset(
    oledBuffer,
    0,
    sizeof(oledBuffer)
  );
}


void oledPixel(
  int x,
  int y,
  bool color = true
)
{
  if (
    x < 0 ||
    x >= OLED_WIDTH ||
    y < 0 ||
    y >= OLED_HEIGHT
  )
  {
    return;
  }


  uint16_t index =
    x +
    (y / 8) *
    OLED_WIDTH;


  uint8_t bit =
    1 << (y & 7);


  if (color)
  {
    oledBuffer[index] |= bit;
  }
  else
  {
    oledBuffer[index] &= ~bit;
  }
}


void oledLine(
  int x1,
  int y1,
  int x2,
  int y2
)
{
  int dx = abs(x2 - x1);
  int sx = x1 < x2 ? 1 : -1;

  int dy = -abs(y2 - y1);
  int sy = y1 < y2 ? 1 : -1;

  int err = dx + dy;


  while (true)
  {
    oledPixel(x1, y1);


    if (
      x1 == x2 &&
      y1 == y2
    )
    {
      break;
    }


    int e2 = 2 * err;


    if (e2 >= dy)
    {
      err += dy;
      x1 += sx;
    }


    if (e2 <= dx)
    {
      err += dx;
      y1 += sy;
    }
  }
}


void oledRect(
  int x,
  int y,
  int w,
  int h
)
{
  oledLine(
    x,
    y,
    x + w - 1,
    y
  );

  oledLine(
    x,
    y + h - 1,
    x + w - 1,
    y + h - 1
  );

  oledLine(
    x,
    y,
    x,
    y + h - 1
  );

  oledLine(
    x + w - 1,
    y,
    x + w - 1,
    y + h - 1
  );
}


void oledFillRect(
  int x,
  int y,
  int w,
  int h
)
{
  for (
    int yy = y;
    yy < y + h;
    yy++
  )
  {
    for (
      int xx = x;
      xx < x + w;
      xx++
    )
    {
      oledPixel(xx, yy);
    }
  }
}


// ============================================================
//                    5x7 FONT
// ============================================================

const uint8_t font5x7[][5] PROGMEM =
{
  {0,0,0,0,0},

  {0x3E,0x51,0x49,0x45,0x3E},
  {0x00,0x42,0x7F,0x40,0x00},
  {0x42,0x61,0x51,0x49,0x46},
  {0x21,0x41,0x45,0x4B,0x31},
  {0x18,0x14,0x12,0x7F,0x10},
  {0x27,0x45,0x45,0x45,0x39},
  {0x3C,0x4A,0x49,0x49,0x30},
  {0x01,0x71,0x09,0x05,0x03},
  {0x36,0x49,0x49,0x49,0x36},
  {0x06,0x49,0x49,0x29,0x1E},

  {0x7E,0x11,0x11,0x11,0x7E},
  {0x7F,0x49,0x49,0x49,0x36},
  {0x3E,0x41,0x41,0x41,0x22},
  {0x7F,0x41,0x41,0x22,0x1C},
  {0x7F,0x49,0x49,0x49,0x41},
  {0x7F,0x09,0x09,0x09,0x01},
  {0x3E,0x41,0x49,0x49,0x7A},
  {0x7F,0x08,0x08,0x08,0x7F},
  {0x00,0x41,0x7F,0x41,0x00},
  {0x20,0x40,0x41,0x3F,0x01},
  {0x7F,0x08,0x14,0x22,0x41},
  {0x7F,0x40,0x40,0x40,0x40},
  {0x7F,0x02,0x0C,0x02,0x7F},
  {0x7F,0x04,0x08,0x10,0x7F},
  {0x3E,0x41,0x41,0x41,0x3E},
  {0x7F,0x09,0x09,0x09,0x06},
  {0x3E,0x41,0x51,0x21,0x5E},
  {0x7F,0x09,0x19,0x29,0x46},
  {0x46,0x49,0x49,0x49,0x31},
  {0x01,0x01,0x7F,0x01,0x01},
  {0x3F,0x40,0x40,0x40,0x3F},
  {0x1F,0x20,0x40,0x20,0x1F},
  {0x7F,0x20,0x18,0x20,0x7F},
  {0x63,0x14,0x08,0x14,0x63},
  {0x03,0x04,0x78,0x04,0x03},
  {0x61,0x51,0x49,0x45,0x43}
};


int charIndex(char c)
{
  if (c == ' ')
    return 0;

  if (c >= '0' && c <= '9')
    return 1 + c - '0';

  if (c >= 'A' && c <= 'Z')
    return 11 + c - 'A';

  return 0;
}


void oledChar(
  int x,
  int y,
  char c,
  uint8_t scale = 1
)
{
  int index =
    charIndex(c);


  for (int col = 0; col < 5; col++)
  {
    uint8_t line =
      pgm_read_byte(
        &font5x7[index][col]
      );


    for (int row = 0; row < 7; row++)
    {
      if (
        line &
        (1 << row)
      )
      {
        for (
          int sx = 0;
          sx < scale;
          sx++
        )
        {
          for (
            int sy = 0;
            sy < scale;
            sy++
          )
          {
            oledPixel(
              x +
              col * scale +
              sx,

              y +
              row * scale +
              sy
            );
          }
        }
      }
    }
  }
}


void oledText(
  int x,
  int y,
  const char* text,
  uint8_t scale = 1
)
{
  while (*text)
  {
    oledChar(
      x,
      y,
      *text,
      scale
    );

    x += 6 * scale;

    text++;
  }
}


// ============================================================
//                    OLED DASHBOARD
// ============================================================

void drawOLED()
{
  static float animatedDuty = 0;

  static unsigned long lastFrame = 0;

  static int lastPercent = -1;


  // OLED animation ~15 FPS

  if (
    millis() - lastFrame < 65
  )
  {
    return;
  }


  lastFrame = millis();


  // Smooth movement

  animatedDuty +=
    (duty - animatedDuty) *
    0.30;


  if (
    abs(
      (int)duty -
      (int)animatedDuty
    ) < 1
  )
  {
    animatedDuty = duty;
  }


  int percent =
    (int)(
      animatedDuty *
      100.0 /
      PWM_MAX +
      0.5
    );


  // Don't redraw if visually unchanged

  if (
    percent == lastPercent
  )
  {
    return;
  }


  lastPercent = percent;


  oledClear();


  // Header

  oledText(
    4,
    1,
    "PWM CONTROLLER",
    1
  );


  oledLine(
    3,
    10,
    124,
    10
  );


  // Percentage

  char percentText[8];

  sprintf(
    percentText,
    "%d%%",
    percent
  );


  int textWidth =
    strlen(percentText) * 18;


  int textX =
    (128 - textWidth) / 2;


  oledText(
    textX,
    16,
    percentText,
    3
  );


  // PWM number

  char pwmText[24];

  sprintf(
    pwmText,
    "PWM %d",
    duty
  );


  oledText(
    4,
    42,
    pwmText,
    1
  );


  // Progress bar

  int barX = 4;
  int barY = 53;

  int barW = 120;
  int barH = 9;


  oledRect(
    barX,
    barY,
    barW,
    barH
  );


  int fill =
    (
      animatedDuty /
      PWM_MAX
    ) *
    (barW - 4);


  if (fill > 0)
  {
    oledFillRect(
      barX + 2,
      barY + 2,
      fill,
      barH - 4
    );
  }


  oledDisplay();
}


// ============================================================
//                    SHA-1
// ============================================================

uint32_t rol(
  uint32_t value,
  uint8_t bits
)
{
  return
    (value << bits) |
    (value >> (32 - bits));
}


void sha1(
  const uint8_t* data,
  size_t length,
  uint8_t output[20]
)
{
  uint32_t h0 = 0x67452301;
  uint32_t h1 = 0xEFCDAB89;
  uint32_t h2 = 0x98BADCFE;
  uint32_t h3 = 0x10325476;
  uint32_t h4 = 0xC3D2E1F0;


  size_t total =
    ((length + 9 + 63) / 64) * 64;


  uint8_t block[64];


  for (
    size_t offset = 0;
    offset < total;
    offset += 64
  )
  {
    memset(
      block,
      0,
      64
    );


    for (
      int i = 0;
      i < 64;
      i++
    )
    {
      size_t pos =
        offset + i;


      if (pos < length)
      {
        block[i] =
          data[pos];
      }
      else if (pos == length)
      {
        block[i] =
          0x80;
      }
    }


    if (
      offset + 64 >= total
    )
    {
      uint64_t bitLength =
        (uint64_t)length * 8;


      for (int i = 0; i < 8; i++)
      {
        block[
          63 - i
        ] =
          bitLength >>
          (i * 8);
      }
    }


    uint32_t w[80];


    for (int i = 0; i < 16; i++)
    {
      w[i] =
        ((uint32_t)block[i * 4] << 24) |
        ((uint32_t)block[i * 4 + 1] << 16) |
        ((uint32_t)block[i * 4 + 2] << 8) |
        ((uint32_t)block[i * 4 + 3]);
    }


    for (int i = 16; i < 80; i++)
    {
      w[i] =
        rol(
          w[i - 3] ^
          w[i - 8] ^
          w[i - 14] ^
          w[i - 16],
          1
        );
    }


    uint32_t a = h0;
    uint32_t b = h1;
    uint32_t c = h2;
    uint32_t d = h3;
    uint32_t e = h4;


    for (int i = 0; i < 80; i++)
    {
      uint32_t f;
      uint32_t k;


      if (i < 20)
      {
        f =
          (b & c) |
          ((~b) & d);

        k =
          0x5A827999;
      }
      else if (i < 40)
      {
        f =
          b ^ c ^ d;

        k =
          0x6ED9EBA1;
      }
      else if (i < 60)
      {
        f =
          (b & c) |
          (b & d) |
          (c & d);

        k =
          0x8F1BBCDC;
      }
      else
      {
        f =
          b ^ c ^ d;

        k =
          0xCA62C1D6;
      }


      uint32_t temp =
        rol(a, 5) +
        f +
        e +
        k +
        w[i];


      e = d;
      d = c;
      c = rol(b, 30);
      b = a;
      a = temp;
    }


    h0 += a;
    h1 += b;
    h2 += c;
    h3 += d;
    h4 += e;
  }


  uint32_t h[5] =
  {
    h0,
    h1,
    h2,
    h3,
    h4
  };


  for (int i = 0; i < 5; i++)
  {
    output[i * 4] =
      h[i] >> 24;

    output[i * 4 + 1] =
      h[i] >> 16;

    output[i * 4 + 2] =
      h[i] >> 8;

    output[i * 4 + 3] =
      h[i];
  }
}


// ============================================================
//                    BASE64
// ============================================================

const char base64Table[] =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
  "abcdefghijklmnopqrstuvwxyz"
  "0123456789+/";


String base64Encode(
  const uint8_t* data,
  size_t length
)
{
  String result;


  for (
    size_t i = 0;
    i < length;
    i += 3
  )
  {
    uint32_t value =
      (uint32_t)data[i] << 16;


    if (i + 1 < length)
      value |=
        (uint32_t)data[i + 1] << 8;


    if (i + 2 < length)
      value |=
        data[i + 2];


    result +=
      base64Table[
        (value >> 18) & 0x3F
      ];

    result +=
      base64Table[
        (value >> 12) & 0x3F
      ];


    if (i + 1 < length)
    {
      result +=
        base64Table[
          (value >> 6) & 0x3F
        ];
    }
    else
    {
      result += '=';
    }


    if (i + 2 < length)
    {
      result +=
        base64Table[
          value & 0x3F
        ];
    }
    else
    {
      result += '=';
    }
  }


  return result;
}


// ============================================================
//              WEBSOCKET HANDSHAKE
// ============================================================

void websocketHandshake(
  const String& key
)
{
  const char* GUID =
    "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";


  String combined =
    key + GUID;


  uint8_t hash[20];


  sha1(
    (const uint8_t*)combined.c_str(),
    combined.length(),
    hash
  );


  String accept =
    base64Encode(
      hash,
      20
    );


  wsClient.print(
    "HTTP/1.1 101 Switching Protocols\r\n"
  );

  wsClient.print(
    "Upgrade: websocket\r\n"
  );

  wsClient.print(
    "Connection: Upgrade\r\n"
  );

  wsClient.print(
    "Sec-WebSocket-Accept: "
  );

  wsClient.print(
    accept
  );

  wsClient.print(
    "\r\n\r\n"
  );


  websocketConnected = true;

  wsLength = 0;


  // Send current PWM to browser

  char value[10];

  sprintf(
    value,
    "%u",
    duty
  );


  // Server-to-client text frame

  wsClient.write(0x81);
  wsClient.write(strlen(value));

  wsClient.write(
    (const uint8_t*)value,
    strlen(value)
  );


  Serial.println(
    "WebSocket connected!"
  );
}


// ============================================================
//              SEND WEBSOCKET TEXT
// ============================================================

void websocketSend(
  const char* message
)
{
  if (
    !websocketConnected ||
    !wsClient.connected()
  )
  {
    return;
  }


  size_t length =
    strlen(message);


  // Small text frame

  wsClient.write(0x81);

  wsClient.write(
    (uint8_t)length
  );

  wsClient.write(
    (const uint8_t*)message,
    length
  );
}


// ============================================================
//              PROCESS WEBSOCKET FRAME
// ============================================================

void processWebSocket()
{
  if (
    !websocketConnected
  )
  {
    return;
  }


  if (
    !wsClient.connected()
  )
  {
    websocketConnected = false;
    wsLength = 0;

    Serial.println(
      "WebSocket disconnected"
    );

    return;
  }


  // Read available bytes

  while (
    wsClient.available()
  )
  {
    if (
      wsLength >=
      sizeof(wsBuffer)
    )
    {
      wsLength = 0;
      break;
    }


    wsBuffer[
      wsLength++
    ] =
      wsClient.read();
  }


  // Process complete frames

  while (wsLength >= 2)
  {
    uint8_t b1 =
      wsBuffer[0];

    uint8_t b2 =
      wsBuffer[1];


    uint8_t opcode =
      b1 & 0x0F;


    bool masked =
      b2 & 0x80;


    uint64_t payloadLength =
      b2 & 0x7F;


    size_t headerLength = 2;


    if (
      payloadLength == 126
    )
    {
      if (wsLength < 4)
        return;


      payloadLength =
        ((uint16_t)wsBuffer[2] << 8) |
        wsBuffer[3];

      headerLength = 4;
    }
    else if (
      payloadLength == 127
    )
    {
      // Not needed for our small messages

      wsLength = 0;
      return;
    }


    if (
      payloadLength > 120
    )
    {
      wsLength = 0;
      return;
    }


    if (masked)
    {
      headerLength += 4;
    }


    size_t frameLength =
      headerLength +
      payloadLength;


    if (
      wsLength < frameLength
    )
    {
      return;
    }


    uint8_t mask[4];


    if (masked)
    {
      for (int i = 0; i < 4; i++)
      {
        mask[i] =
          wsBuffer[
            headerLength - 4 + i
          ];
      }
    }


    // --------------------------------------------------------
    // TEXT FRAME
    // --------------------------------------------------------

    if (
      opcode == 0x1
    )
    {
      char message[121];


      for (
        size_t i = 0;
        i < payloadLength;
        i++
      )
      {
        uint8_t value =
          wsBuffer[
            headerLength + i
          ];


        if (masked)
        {
          value ^=
            mask[i % 4];
        }


        message[i] =
          value;
      }


      message[
        payloadLength
      ] = '\0';


      int newDuty =
        atoi(message);


      newDuty =
        constrain(
          newDuty,
          0,
          PWM_MAX
        );


      duty =
        (uint16_t)newDuty;


      // IMPORTANT:
      // PWM changes immediately

      ledcWrite(
        PWM_PIN,
        duty
      );


      // Send current value back

      websocketSend(
        message
      );
    }


    // --------------------------------------------------------
    // CLOSE
    // --------------------------------------------------------

    else if (
      opcode == 0x8
    )
    {
      wsClient.stop();

      websocketConnected = false;

      wsLength = 0;

      Serial.println(
        "WebSocket closed"
      );

      return;
    }


    // --------------------------------------------------------
    // PING
    // --------------------------------------------------------

    else if (
      opcode == 0x9
    )
    {
      // Pong

      wsClient.write(
        0x8A
      );

      wsClient.write(
        (uint8_t)payloadLength
      );


      for (
        size_t i = 0;
        i < payloadLength;
        i++
      )
      {
        uint8_t value =
          wsBuffer[
            headerLength + i
          ];


        if (masked)
        {
          value ^=
            mask[i % 4];
        }


        wsClient.write(
          value
        );
      }
    }


    // Remove processed frame

    size_t remaining =
      wsLength -
      frameLength;


    if (remaining > 0)
    {
      memmove(
        wsBuffer,
        wsBuffer + frameLength,
        remaining
      );
    }


    wsLength =
      remaining;
  }
}


// ============================================================
//                    HTML DASHBOARD
// ============================================================

const char INDEX_HTML[] PROGMEM = R"HTML(

<!DOCTYPE html>

<html>

<head>

<meta name="viewport"
content="width=device-width,
initial-scale=1,
maximum-scale=1,
user-scalable=no">

<title>ESP32-C3 PWM</title>


<style>

*{
    box-sizing:border-box;
    -webkit-tap-highlight-color:transparent;
}

body{

    margin:0;

    min-height:100vh;

    display:flex;

    justify-content:center;

    align-items:center;

    font-family:
    Arial,
    Helvetica,
    sans-serif;

    color:white;

    background:
    radial-gradient(
        circle at 20% 0%,
        #172554,
        #020617 65%
    );

}


.card{

    width:min(92vw,450px);

    padding:28px;

    border-radius:28px;

    background:
    rgba(15,23,42,.88);

    border:
    1px solid
    rgba(255,255,255,.12);

    box-shadow:
    0 25px 80px
    rgba(0,0,0,.55);

}


.header{

    display:flex;

    justify-content:space-between;

    align-items:center;

    margin-bottom:20px;

}


.title{

    font-size:17px;

    font-weight:800;

    letter-spacing:1.5px;

}


.status{

    font-size:11px;

    color:#86efac;

}


.status.offline{

    color:#f87171;

}


.status::before{

    content:"";

    display:inline-block;

    width:8px;

    height:8px;

    margin-right:6px;

    border-radius:50%;

    background:#22c55e;

    box-shadow:
    0 0 12px #22c55e;

}


.status.offline::before{

    background:#ef4444;

    box-shadow:
    0 0 12px #ef4444;

}


.percent{

    text-align:center;

    font-size:76px;

    font-weight:900;

    letter-spacing:-5px;

    margin:
    15px 0 30px;

    background:
    linear-gradient(
        135deg,
        #fff,
        #60a5fa
    );

    -webkit-background-clip:text;

    -webkit-text-fill-color:transparent;

    transition:
    transform .08s ease;

}


.sliderBox{

    padding:
    8px 0;

}


input[type=range]{

    width:100%;

    height:12px;

    appearance:none;

    -webkit-appearance:none;

    border-radius:20px;

    outline:none;

    background:
    linear-gradient(
        to right,
        #3b82f6 0%,
        #3b82f6 var(--p),
        #1e293b var(--p),
        #1e293b 100%
    );

}


input[type=range]::-webkit-slider-thumb{

    appearance:none;

    -webkit-appearance:none;

    width:30px;

    height:30px;

    border-radius:50%;

    background:white;

    border:
    4px solid #3b82f6;

    box-shadow:
    0 0 20px
    rgba(59,130,246,.9);

}


.labels{

    display:flex;

    justify-content:space-between;

    margin-top:10px;

    color:#64748b;

    font-size:12px;

}


.info{

    margin-top:25px;

    padding:18px;

    border-radius:18px;

    background:
    rgba(255,255,255,.05);

}


.row{

    display:flex;

    justify-content:space-between;

    margin:10px 0;

    font-size:14px;

}


.label{

    color:#94a3b8;

}


.value{

    font-weight:700;

}


.buttons{

    display:grid;

    grid-template-columns:
    repeat(5,1fr);

    gap:7px;

    margin-top:20px;

}


button{

    border:0;

    padding:11px 3px;

    border-radius:12px;

    background:#1e293b;

    color:#e2e8f0;

    font-weight:700;

    cursor:pointer;

    transition:
    transform .08s ease,
    background .15s ease;

}


button:active{

    transform:scale(.93);

}


.footer{

    margin-top:22px;

    text-align:center;

    color:#64748b;

    font-size:11px;

    letter-spacing:1.2px;

    font-weight:600;

}

</style>

</head>


<body>


<div class="card">


<div class="header">

<div class="title">
PWM CONTROLLER
</div>

<div
class="status"
id="status">

CONNECTING

</div>

</div>


<div
class="percent"
id="percent">

0%

</div>


<div class="sliderBox">

<input
id="slider"
type="range"
min="0"
max="4095"
value="0"
style="--p:0%">

<div class="labels">

<span>0%</span>

<span>50%</span>

<span>100%</span>

</div>

</div>


<div class="info">


<div class="row">

<span class="label">
DUTY CYCLE
</span>

<span
class="value"
id="duty">

0%

</span>

</div>


<div class="row">

<span class="label">
PWM VALUE
</span>

<span
class="value"
id="value">

0 / 4095

</span>

</div>


<div class="row">

<span class="label">
FREQUENCY
</span>

<span class="value">
5 kHz
</span>

</div>


<div class="row">

<span class="label">
RESOLUTION
</span>

<span class="value">
12-bit
</span>

</div>


</div>


<div class="buttons">

<button onclick="setPWM(0)">
0%
</button>

<button onclick="setPWM(1024)">
25%
</button>

<button onclick="setPWM(2048)">
50%
</button>

<button onclick="setPWM(3072)">
75%
</button>

<button onclick="setPWM(4095)">
100%
</button>

</div>


<div class="footer">
Powered by CholoBiddut
</div>


</div>


<script>


// =================================================
// VARIABLES
// =================================================

const slider =
document.getElementById("slider");

const percent =
document.getElementById("percent");

const duty =
document.getElementById("duty");

const value =
document.getElementById("value");

const status =
document.getElementById("status");


let socket = null;

let reconnectTimer = null;


// =================================================
// UPDATE UI
// =================================================

function updateUI(v)
{

    v =
    Math.max(
        0,
        Math.min(
            4095,
            v
        )
    );


    const p =
    (v / 4095) * 100;


    slider.value = v;


    slider.style
    .setProperty(
        "--p",
        p + "%"
    );


    percent.innerText =
    p.toFixed(1) + "%";


    duty.innerText =
    p.toFixed(1) + "%";


    value.innerText =
    v + " / 4095";


    percent.style.transform =
    "scale(1.035)";


    requestAnimationFrame(
        () =>
        {
            percent.style.transform =
            "scale(1)";
        }
    );
}


// =================================================
// WEBSOCKET CONNECT
// =================================================

function connect()
{

    status.innerText =
    "CONNECTING";

    status.classList
    .add("offline");


    socket =
    new WebSocket(
        "ws://" +
        window.location.host +
        "/"
    );


    // ------------------------------------------------
    // CONNECTED
    // ------------------------------------------------

    socket.onopen =
    function()
    {

        status.innerText =
        "ONLINE";

        status.classList
        .remove("offline");

    };


    // ------------------------------------------------
    // MESSAGE
    // ------------------------------------------------

    socket.onmessage =
    function(event)
    {

        const v =
        Number(event.data);


        if (
            !Number.isNaN(v)
        )
        {
            updateUI(v);
        }

    };


    // ------------------------------------------------
    // CLOSED
    // ------------------------------------------------

    socket.onclose =
    function()
    {

        status.innerText =
        "RECONNECTING";

        status.classList
        .add("offline");


        clearTimeout(
            reconnectTimer
        );


        reconnectTimer =
        setTimeout(
            connect,
            700
        );

    };


    // ------------------------------------------------
    // ERROR
    // ------------------------------------------------

    socket.onerror =
    function()
    {

        socket.close();

    };

}


// =================================================
// SEND PWM
// =================================================

function sendPWM(v)
{

    if (
        socket &&
        socket.readyState ===
        WebSocket.OPEN
    )
    {

        socket.send(
            String(v)
        );

    }

}


// =================================================
// SLIDER
// =================================================

slider.addEventListener(
    "input",
    function()
    {

        const v =
        Number(this.value);


        // UI changes instantly

        updateUI(v);


        // PWM changes instantly

        sendPWM(v);

    }
);


// =================================================
// QUICK BUTTONS
// =================================================

function setPWM(v)
{

    updateUI(v);

    sendPWM(v);

}


// =================================================
// START
// =================================================

updateUI(0);

connect();


</script>


</body>

</html>

)HTML";


// ============================================================
//                    HTTP RESPONSE
// ============================================================

void sendHTML()
{
  wsClient.print(
    "HTTP/1.1 200 OK\r\n"
  );

  wsClient.print(
    "Content-Type: text/html\r\n"
  );

  wsClient.print(
    "Connection: close\r\n"
  );

  wsClient.print(
    "Cache-Control: no-store\r\n"
  );

  wsClient.print(
    "Content-Length: "
  );

  wsClient.print(
    strlen(INDEX_HTML)
  );

  wsClient.print(
    "\r\n\r\n"
  );


  // Send HTML from flash

  wsClient.write(
    (const uint8_t*)INDEX_HTML,
    strlen(INDEX_HTML)
  );
}


// ============================================================
//                    HTTP PROCESSING
// ============================================================

void processHTTP()
{
  if (
    !wsClient.connected()
  )
  {
    return;
  }


  while (
    wsClient.available()
  )
  {
    if (
      httpLength >=
      sizeof(httpBuffer) - 1
    )
    {
      wsClient.stop();

      httpLength = 0;

      return;
    }


    httpBuffer[
      httpLength++
    ] =
      wsClient.read();


    httpBuffer[
      httpLength
    ] = '\0';


    // End of HTTP headers

    if (
      httpLength >= 4 &&
      strstr(
        httpBuffer,
        "\r\n\r\n"
      )
    )
    {
      break;
    }
  }


  if (
    httpLength < 4
  )
  {
    return;
  }


  // ----------------------------------------------------------
  // WebSocket Upgrade?
  // ----------------------------------------------------------

  if (
    strstr(
      httpBuffer,
      "Upgrade: websocket"
    ) ||
    strstr(
      httpBuffer,
      "Upgrade: WebSocket"
    )
  )
  {
    char* keyStart =
      strstr(
        httpBuffer,
        "Sec-WebSocket-Key:"
      );


    if (keyStart)
    {
      keyStart +=
        strlen(
          "Sec-WebSocket-Key:"
        );


      while (
        *keyStart == ' '
      )
      {
        keyStart++;
      }


      char key[100];

      int i = 0;


      while (
        *keyStart &&
        *keyStart != '\r' &&
        *keyStart != '\n' &&
        i < 99
      )
      {
        key[i++] =
          *keyStart++;
      }


      key[i] =
        '\0';


      String wsKey =
        String(key);


      wsKey.trim();


      websocketHandshake(
        wsKey
      );


      httpLength = 0;

      return;
    }
  }


  // ----------------------------------------------------------
  // Normal HTTP request
  // ----------------------------------------------------------

  if (
    strstr(
      httpBuffer,
      "GET / "
    ) ||
    strstr(
      httpBuffer,
      "GET /HTTP"
    )
  )
  {
    sendHTML();
  }


  wsClient.stop();

  httpLength = 0;
}


// ============================================================
//                    NEW CLIENT
// ============================================================

void acceptNewClient()
{
  if (
    websocketConnected
  )
  {
    return;
  }


  WiFiClient newClient =
    server.available();


  if (
    newClient
  )
  {
    wsClient =
      newClient;


    wsClient.setTimeout(50);


    httpLength = 0;

    wsLength = 0;


    Serial.println(
      "New client connected"
    );
  }
}


// ============================================================
//                    OLED START SCREEN
// ============================================================

void oledReadyScreen()
{
  oledClear();


  oledText(
    4,
    2,
    "PWM CONTROLLER",
    1
  );


  oledLine(
    3,
    10,
    124,
    10
  );


  oledText(
    25,
    24,
    "READY",
    2
  );


  oledText(
    18,
    48,
    "192.168.4.1",
    1
  );


  oledDisplay();
}


// ============================================================
//                    SETUP
// ============================================================

void setup()
{
  Serial.begin(115200);


  delay(300);


  // ----------------------------------------------------------
  // OLED
  // ----------------------------------------------------------

  oledInit();


  oledClear();

  oledText(
    15,
    27,
    "STARTING",
    1
  );

  oledDisplay();


  // ----------------------------------------------------------
  // PWM
  // ----------------------------------------------------------

  bool pwmOK =
    ledcAttach(
      PWM_PIN,
      PWM_FREQUENCY,
      PWM_RESOLUTION
    );


  if (!pwmOK)
  {
    Serial.println(
      "PWM ATTACH FAILED"
    );


    oledClear();

    oledText(
      15,
      27,
      "PWM ERROR",
      1
    );

    oledDisplay();


    while (true)
    {
      delay(1000);
    }
  }


  ledcWrite(
    PWM_PIN,
    0
  );


  // ----------------------------------------------------------
  // ACCESS POINT
  // ----------------------------------------------------------

  WiFi.mode(
    WIFI_AP
  );


  bool apOK =
    WiFi.softAP(
      AP_SSID,
      AP_PASSWORD
    );


  if (!apOK)
  {
    Serial.println(
      "AP START FAILED"
    );


    while (true)
    {
      delay(1000);
    }
  }


  // ----------------------------------------------------------
  // SERVER
  // ----------------------------------------------------------

  server.begin();


  // ----------------------------------------------------------
  // SERIAL
  // ----------------------------------------------------------

  Serial.println();
  Serial.println(
    "================================"
  );

  Serial.println(
    "     ESP32-C3 PWM CONTROLLER"
  );

  Serial.println(
    "================================"
  );


  Serial.print(
    "SSID       : "
  );

  Serial.println(
    AP_SSID
  );


  Serial.print(
    "PASSWORD   : "
  );

  Serial.println(
    AP_PASSWORD
  );


  Serial.print(
    "IP ADDRESS : "
  );

  Serial.println(
    WiFi.softAPIP()
  );


  Serial.println(
    "SERVER     : READY"
  );

  Serial.println(
    "WEBSOCKET  : CUSTOM"
  );

  Serial.println(
    "PWM PIN    : GPIO10"
  );

  Serial.println(
    "PWM FREQ   : 5 kHz"
  );

  Serial.println(
    "PWM RES    : 12-bit"
  );

  Serial.println(
    "OLED SDA   : GPIO6"
  );

  Serial.println(
    "OLED SCL   : GPIO7"
  );


  Serial.println(
    "================================"
  );


  oledReadyScreen();

  delay(1200);
}


// ============================================================
//                    LOOP
// ============================================================

void loop()
{
  // Accept browser connection

  acceptNewClient();


  // HTTP → WebSocket handshake

  if (
    !websocketConnected &&
    wsClient &&
    wsClient.connected()
  )
  {
    processHTTP();
  }


  // Real-time WebSocket

  if (
    websocketConnected
  )
  {
    processWebSocket();
  }


  // OLED animation

  drawOLED();


  // Keep loop extremely responsive

  delay(1);
}