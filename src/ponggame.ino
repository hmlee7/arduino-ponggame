#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>

// display device
const int OLED_RESET        = -1;
const int SCREEN_WIDTH      = 128;  //real size minus 1, because coordinate system starts with 0
const int SCREEN_HEIGHT     = 64;   //real size minus 1, because coordinate system starts with 0
const int FONT_SIZE         = 2;

// I/O pin numbers
const int PADDLE_PIN_A      = A0;
const int PADDLE_PIN_B      = A1;
const int BUZZER_PIN        = 3;

// app constants
const int SCORE_PADDING     = 10;
const int TABLE_OFFSET_X    = 0;
const int TABLE_OFFSET_Y    = 0;
const int PADDLE_WIDTH      = 4;
const int PADDLE_HEIGHT     = 15;
const int BALL_SIZE         = 3;
const int BALL_MAX_Y_SPEED  = PADDLE_HEIGHT/2;
const int MAX_SCORE         = 5;    // game over score point

class PongDisplay : public Adafruit_SSD1306 {
public:
    PongDisplay() : Adafruit_SSD1306(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET) {}

    void begin()
    {
        Adafruit_SSD1306::begin(SSD1306_SWITCHCAPVCC, 0x3C); // initialize with the I2C addr 0x3D (for the 128x64)
        setTextWrap(false);
        clearDisplay();                    // clears the screen and buffer
    }

    void centerPrint(const char *text, int y, int size)
    {
        setTextSize(size);
        setCursor(SCREEN_WIDTH / 2 - ((strlen(text)) * 6 * size) / 2, y);
        print(text);
    }
};

PongDisplay display;

class Ball {
protected:
    int     m_px    = 0;    // x position
    int     m_py    = 0;    // y position
    int     m_vx    = 2;    // x velocity
    int     m_vy    = 1;    // y velocity
    int     m_size  = 3;    // ball size

public:
    Ball(int size=3) : m_size(size) {}

    void reset(int px, int py) { m_px = px; m_py = py; m_vx=2; m_vy=1; }
    int px() const { return m_px; }
    int py() const { return m_py; }
    int vx() const { return m_vx; }
    int vy() const { return m_vy; }
    int size() const { return m_size; }

    void flip_vx() { m_vx = -m_vx; }
    void flip_vy() { m_vy = -m_vy; }
    void accel_vy(int amount) { m_vy += amount; }

    void paint(int color=WHITE) { display.fillRect(m_px, m_py, m_size, m_size, color); }

    void update()
    {
        m_px += m_vx;
        m_py += m_vy;
        // dmsg("px=%d py=%d vx=%d vy=%d\n", m_px, m_py, m_vx, m_vy);
    }
};

class Paddle {
protected:
    int     m_pin_id        = -1;
    int     m_px            = 0;    // x position
    int     m_py            = 0;    // y position
    int     m_py_prev       = 0;    // remember prev position for ball acceleration
    int     m_range         = 0;    // paddle moving range

public:
    Paddle(int pin_id, int xpos, int range) : m_pin_id(pin_id), m_px(xpos), m_range(range) {}

    int py() const { return m_py; }
    int velovity() { return m_py - m_py_prev; }
    int speed() { return abs(velovity()); }

    bool overlaps(int px, int py) const {
        return (px>=m_px && px<=m_px+PADDLE_WIDTH) && (py>=m_py && py<=m_py+PADDLE_HEIGHT);
    }
    
    bool overlaps(const Ball& ball) const {
        // if ball is moving from left to right, then check right egde position of the ball
        if (ball.px()<m_px && ball.vx()>0) return overlaps(ball.px()+ball.size(), ball.py());

        // otherwise, check left edge position of the ball
        return overlaps(ball.px(), ball.py());
    }

    void update()
    {
        m_py_prev = m_py;
        m_py = (int)map(analogRead(m_pin_id), 0, 1023, 0, m_range-PADDLE_HEIGHT)
            + (SCREEN_HEIGHT - m_range)/2;  // offset from the screen
    }

    void paint(int color=WHITE) { display.fillRect(m_px, m_py, PADDLE_WIDTH, PADDLE_HEIGHT, color); }
};

class PongTable {
protected:
    int         m_offs_x        = 0;
    int         m_offs_y        = 0;

public:

public:
    PongTable(int offs_x, int offs_y) : m_offs_x(offs_x), m_offs_y(offs_y) {}

    int left() const { return m_offs_x; }
    int top() const { return m_offs_y; }
    int right() const { return SCREEN_WIDTH-m_offs_x-1; }
    int bottom() const { return SCREEN_HEIGHT-m_offs_y-1; }
    int width() const { return SCREEN_WIDTH - m_offs_x*2; }
    int height() const { return SCREEN_HEIGHT - m_offs_y*2; }
    int center_x() const { return m_offs_x + (SCREEN_WIDTH - m_offs_x*2)/2; }
    int center_y() const { return m_offs_y + (SCREEN_HEIGHT - m_offs_y*2)/2; }

    // bool is_ball_out() const { return m_ball.px()<left() || m_ball.px()>right(); }
    bool ball_out_left(const Ball& ball) const { return ball.px()<left()-1 && ball.vx()<0; }
    bool ball_out_right(const Ball& ball) const { return ball.px()>right()+1 && ball.vx()>0; }
    bool ball_out_top(const Ball& ball) const { return ball.py()<=top() && ball.vy()<0; }
    bool ball_out_bottom(const Ball& ball) const { return ball.py()>=bottom() && ball.vy()>0; }

    void paint(int color=WHITE)
    {
        int table_height = height();
        display.drawFastVLine(left(), top(), table_height, color);
        display.drawFastVLine(right(), top(), table_height, color);
        display.drawFastHLine(left(), top(), width(), color);
        display.drawFastHLine(left(), bottom(), width(), color);

         // draw dotted center line
         int cx = center_x();
        for (int i=0; i<table_height; i+=4) {
            display.drawFastVLine(cx, m_offs_y+i, 2, color);
        }
    }
};

class PongGame {
protected:
    int         m_scoreL    = 0;
    int         m_scoreR    = 0;
    PongTable   m_table;
    Paddle      m_paddleL;
    Paddle      m_paddleR;
    Ball        m_ball;
    bool        m_active = false;

public:
    PongGame() : m_table(TABLE_OFFSET_X, TABLE_OFFSET_Y),
        m_paddleL(PADDLE_PIN_A, TABLE_OFFSET_X, m_table.height()),
        m_paddleR(PADDLE_PIN_B, m_table.right()-PADDLE_WIDTH, m_table.height()),
        m_ball(BALL_SIZE) {}

    void init()
    {
        display.begin();
        display.setTextColor(WHITE);
        display.centerPrint("PONG GAME", 10, 2);
        display.fillRect(0, SCREEN_HEIGHT - 30, SCREEN_WIDTH - 10, 30, WHITE);
        display.setTextColor(BLACK);
        display.centerPrint("Move paddle", SCREEN_HEIGHT - 25, 1);
        display.centerPrint("to start!", SCREEN_HEIGHT - 15, 1);
        display.display();

        // scan initial paddle position
        m_paddleL.update();
        m_paddleR.update();
    }

    void start()
    {
        m_scoreL = 0;
        m_scoreR = 0;
        m_ball.reset(m_table.left(), m_paddleL.py() + PADDLE_HEIGHT/2);

        // clear screen
        display.setTextColor(WHITE);
        display.setTextSize(FONT_SIZE);
        display.clearDisplay();
    
        sound_start();
        m_active = true;
    }

    void wait_for_player_action() {
        int posL = m_paddleL.py();
        int posR = m_paddleR.py();
        while (abs(posL - m_paddleL.py()) + abs(posR - m_paddleR.py()) < 5) {
            m_paddleL.update();
            m_paddleR.update();
        }
        start();
    }

    void check_bounce()
    {
        // check table top/bottom bouncing
        if (m_table.ball_out_top(m_ball) || m_table.ball_out_bottom(m_ball)) {
            m_ball.flip_vy();
            sound_bounce();
        }

        // check left paddle stroke
        if (m_paddleL.overlaps(m_ball) && m_ball.vx()<0) {
            m_ball.flip_vx();
            // accelerate ball speed proportional to paddle swing distance
            int accel = round((float)BALL_MAX_Y_SPEED*m_paddleL.speed() / PADDLE_HEIGHT);
            m_ball.accel_vy(accel);
            sound_bounce();
        }

        // check right paddle stroke
        if (m_paddleR.overlaps(m_ball) && m_ball.vx()>0) {
            m_ball.flip_vx();
            int accel = round((float)BALL_MAX_Y_SPEED*m_paddleR.speed() / PADDLE_HEIGHT);
            m_ball.accel_vy(accel);
            sound_bounce();
        }
    }

    void update_score()
    {
        if (m_table.ball_out_left(m_ball)) {
            ++m_scoreR;
            m_ball.reset(m_table.right(), m_paddleR.py() + PADDLE_HEIGHT/2);
            refresh_sceen();
            sound_score();
            delay(1000);
            m_ball.flip_vx();
        }
        else if (m_table.ball_out_right(m_ball)) {
            ++m_scoreL;
            m_ball.reset(m_table.left(), m_paddleL.py() + PADDLE_HEIGHT/2);
            refresh_sceen();
            sound_score();
            delay(1000);
        }
    }

    void print_score()
    {
        // backwards indent score A. This is dirty, but it works ... ;)
        int scoreAWidth = 5 * FONT_SIZE;
        if (m_scoreL > 9) scoreAWidth += 6 * FONT_SIZE;
        if (m_scoreL > 99) scoreAWidth += 6 * FONT_SIZE;
        if (m_scoreL > 999) scoreAWidth += 6 * FONT_SIZE;
        if (m_scoreL > 9999) scoreAWidth += 6 * FONT_SIZE;

        display.setCursor(SCREEN_WIDTH/2 - SCORE_PADDING - scoreAWidth, 3);
        display.print(m_scoreL);

        display.setCursor(SCREEN_WIDTH/2 + SCORE_PADDING + 1, 3); //+1 because of dotted line.
        display.print(m_scoreR);
        display.display();
    }

    void game_over()
    {
        m_active = false;
        display.clearDisplay();
        display.centerPrint("GAME OVER", 10, 2);
        display.fillRect(0, SCREEN_HEIGHT - 30, SCREEN_WIDTH - 10, 30, WHITE);
        display.setTextColor(BLACK);
        display.centerPrint("Move paddle", SCREEN_HEIGHT - 25, 1);
        display.centerPrint("to start!", SCREEN_HEIGHT - 15, 1);
        display.display();
    }

    void refresh_sceen()
    {
        display.clearDisplay();
        m_table.paint();
        m_ball.paint();
        m_paddleL.paint();
        m_paddleR.paint();
        print_score();
    }

    void run()
    {
        if (!m_active) wait_for_player_action();
    
        m_ball.update();
        m_paddleL.update();
        m_paddleR.update();
        
        check_bounce();
        update_score();
        refresh_sceen();
    
        if (m_scoreL>=MAX_SCORE || m_scoreR>=MAX_SCORE) game_over();
    }

protected:
    void sound_start()
    {
        tone(BUZZER_PIN, 250);
        delay(100);
        tone(BUZZER_PIN, 500);
        delay(100);
        tone(BUZZER_PIN, 1000);
        delay(100);
        noTone(BUZZER_PIN);
    }
    void sound_bounce() { tone(BUZZER_PIN, 500, 50); }
    void sound_score() { tone(BUZZER_PIN, 150, 150); }
};

PongGame    game;

void setup()
{
    Serial.begin(115200);
    game.init();
}

void loop()
{
    game.run();
}
