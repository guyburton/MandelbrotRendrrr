#include    <stdint.h>
#include    <stdio.h>
#include    <string.h>
#include    <math.h>
#include    <unistd.h>
#include    <OpenGL/gl.h>
#include    <OpenGL/glu.h>
#include    <GLUT/glut.h>

#define SIZE 256 
#define ITERATION_THRESHOLD 50

#define clamp(a) (((a) < 0) ? 0 : (((a) > 255) ? 255 : (a)))
#define min(a,b) ((a) < (b) ? (a) : (b))

float X_OFFSET = -0.75f;
float Y_OFFSET = 0.0f;
float SCALE = 0.75f;

GLuint texture[1];
uint8_t data[SIZE*SIZE*3];
float ratio;

uint32_t hsvLut[256];
float normalizeLut[SIZE];

float xrot, yrot, zrot;
float xpan, ypan;
float scale = 1.0f;

int frame = 0;
int timebase = 0;

void hsvToRgb(double h, double S, double V, uint8_t* r, uint8_t* g, uint8_t* b);

void generateSetAsm() {
    asm (
        "push %rax                      ;"
        "push %rdx                      ;"   
        "push %rbx                      ;"
        "pushq $0                       ;" // i
        "pushq $0                       ;" // j

"loop:"
        "movq (%rsp), %rbx              ;"
        "lea _normalizeLut(%rip), %rax  ;"
        "fld (%rax, %rbx, 4)            ;" // f_real
        "fdiv _scale(%rip)              ;" 
        "fadd _X_OFFSET(%rip)           ;" 
        
        "movq 8(%rsp), %rbx             ;"
        "lea _normalizeLut(%rip), %rax  ;"
        "fld (%rax, %rbx, 4)            ;" // f_imag
        "fdiv _scale(%rip)              ;" 
        "fadd _Y_OFFSET(%rip)           ;" 

        "mov $0, %rdx                   ;" // count (%rdx)
        "fldz                           ;" // z_real
        "fldz                           ;" // z_imag
        "fldz                           ;" // size

"inner_loop:"

        "fld  %ST(2)         ;" 
        "fmul %ST(0)         ;" // z_real2 = z_real * z_real  
        "fld  %ST(2)         ;" 
        "fmul %ST(0)         ;" // z_imag2 = z_imag * z_imag
        "fld  %ST(0)         ;"
        "fadd %ST(2)         ;" // size = z_real2 + z_imag2
        "fstp %ST(3)         ;"
        "fld  %ST(1)         ;"
        "fsub %ST(1)         ;" // z_real_new = z_real2 - z_imag2

        // clean z_real2 and z_imag2 from stack
        "fstp %ST(2)          ;" 
        "fstp %ST(0)          ;"

        // z_imag = 2 * z_imag * z_real + c_imag;
        "push $2             ;"
        "fild (%rsp)         ;"
        "pop %rbx            ;"
        "fmul %ST(3)         ;"
        "fmul %ST(4)         ;"
        "fadd %ST(5)         ;"
        "fstp %ST(3)         ;" 

        // z_real = z_real_new + c_real;
        "fadd %ST(5)         ;"
        "fstp %ST(3)         ;"

        "incq %rdx           ;" // i++
        "push $4             ;"
        "fild (%rsp)         ;"
        "pop  %rbx           ;"
        "fcomp %ST(1)         ;" // SIZE < 4 
        "fstsw %ax           ;" 
        "test $0x100, %ax    ;"
        "jnz out_of_loop     ;"
        "cmpq $50, %rdx      ;" // count < 100
        "jl   inner_loop     ;"  

"out_of_loop:"

        //int position = i * SIZE * 3 + j * 3;
        "push  %rdx              ;" // push count
        "movq  $768,    %rbx     ;" // 256 * 3  
        "movq  16(%rsp), %rax    ;" // i
        "mul   %rbx              ;" // * -> edx:eax
        "push  %rax              ;"

        "movq  $3, %rbx          ;" // 3
        "movq  16(%rsp), %rax    ;" // j
        "mul   %rbx              ;" // * -> edx:eax

        "pop   %rbx              ;"
        "add   %rax,  %rbx       ;" // rbx now contains position
    
        "pop   %rdx              ;" // restore count
        "pushq  %rbx             ;" // store position

        // color = 256.0f * count / ITERATION_THRESHOLD * 2.0f;
        "push  %rdx                 ;"
        "pushq $50                  ;" 
        "pushq $256                 ;" 
        "fild  (%rsp)               ;"
        "fild  16(%rsp)             ;"
        "fmulp                      ;" // 256 * count  
        "fild  8(%rsp)              ;"
        "fdivrp                     ;" //  divide (by 8)
        "popq  %rbx                 ;" 
        "fistp (%rsp)               ;"  
        "popq  %rbx                 ;" // rbx now contains color index
        "popq  %rdx                 ;" // rdx now countains count

        // pixelcolor = 0xffffff & hsvLut[color]; 
        "lea  _hsvLut(%rip), %rax   ;"
        "mov  (%rax, %rbx, 4), %rbx ;" // get rgb colour (3 bytes) -> rbx
        "and  $0xFFFFFF, %rbx       ;" // mask top byte

        "popq  %rsi                 ;" // get position
        // pixel* = (uint32_t*)&data[position];
        "lea  _data(%rip), %rax     ;" // load RGB array
        "lea  (%rax, %rsi, 1), %rax ;" // data[position]
        "or   %rbx, (%rax)          ;" // copy bytes to output
    
        "fstp  %st(0)               ;"
        "fstp  %st(0)               ;"
        "fstp  %st(0)               ;"
        "fstp  %st(0)               ;"
        "fstp  %st(0)               ;"

        "incq  (%rsp)               ;" // i++
        "cmpq $256, (%rsp)          ;"
        "jl   loop                  ;" // i loop termination

        "movq $0, (%rsp)            ;" // i = 0
        "incq 8(%rsp)               ;" // j
        "cmpw $256, 8(%rsp)         ;"
        "jl loop                    ;" // j loop termination

        "pop %rax                   ;"
        "pop %rax                   ;"
        "pop %rbx                   ;"
        "pop %rdx                   ;"
        "pop %rax                   ;"
    );
}

void generateSet() {
    for (int j=0; j<SIZE; ++j) {
        for (int i=0; i<SIZE; ++i) {
            float c_real = normalizeLut[j] / scale + X_OFFSET + xpan;
            float c_imag = normalizeLut[i] / scale + Y_OFFSET + ypan;
            float z_real=0.0f, z_imag=0.0f;
            int count = 0;
            float size = 0.0f;
            while( size < 4 && ++count < ITERATION_THRESHOLD) {
              float z_real_2 = z_real * z_real;
              float z_imag_2 = z_imag * z_imag;
              float z_real_new = z_real_2 - z_imag_2;
              z_imag = 2 * z_imag * z_real + c_imag;
              z_real = z_real_new + c_real;

              size = z_real_2 + z_imag_2;
            }

            int position = i * SIZE * 3 + j * 3;
            uint8_t color = (uint8_t)(256.0f * (float)count / (float)ITERATION_THRESHOLD) * 2.0f;
            uint32_t* pixel = (uint32_t*)&data[position];
            *pixel = 0xffffff & hsvLut[color]; 
        }
    }
    glPixelStorei ( GL_UNPACK_ALIGNMENT, 1 );
    glGenTextures(1, &texture[0]);
    glBindTexture(GL_TEXTURE_2D, texture[0]);
    glTexImage2D(GL_TEXTURE_2D, 0,3, SIZE, SIZE, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
}

void reshape( int w, int h )
{
    if(h == 0)
        h = 1;

    ratio = 1.0f * w / h;
    // Reset the coordinate system before modifying
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();

    // Set the viewport to be the entire window
    glViewport(0, 0, w, h);

    //     Set the clipping volume
    gluPerspective(80,ratio,1,200);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    gluLookAt(0, 0, 30,
              0, 0, 10,
              0.0f,1.0f,0.0f);
}

void init() {
    glEnable(GL_TEXTURE_2D);
    glEnable ( GL_COLOR_MATERIAL );
    glEnable ( GL_CULL_FACE );
    glColorMaterial ( GL_FRONT, GL_AMBIENT_AND_DIFFUSE );

    glShadeModel(GL_SMOOTH);
    glClearColor(0.0f, 0.0f, 0.0f, 0.5f);
    //glClearDepth(1.0f);
    glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
   
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    //precompute LUTs
    for (int i=0; i<256; i++ ){
        uint8_t r,g,b;
        hsvToRgb(((float)i / 256.0f) * 360.0f, 1.0f, 1.0f, &r, &g, &b); 
        hsvLut[i] = (r << 16 | g << 8 | b);
    }
    for (int i=0; i<SIZE; i++) 
    {
        normalizeLut[i] = (((float)i / (float)SIZE) * 2.0f - 1.0f) / SCALE;
    }
}


void display() {
    generateSetAsm();

    glPixelStorei ( GL_UNPACK_ALIGNMENT, 1 );
    glGenTextures(1, &texture[0]);
    glBindTexture(GL_TEXTURE_2D, texture[0]);
    glTexImage2D(GL_TEXTURE_2D, 0,3, SIZE, SIZE, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);

    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

    glLoadIdentity(); 
    glPushMatrix();
    glTranslatef ( 0.0, 0.0, -3.0f );
    glRotatef ( xrot, 1.0, 0.0, 0.0 );
    glRotatef ( yrot, 0.0, 1.0, 0.0 );
    glRotatef ( zrot, 0.0, 0.0, 1.0 );
    glBindTexture(GL_TEXTURE_2D, texture[0]);
    glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f); glVertex3f(-1.0f, -1.0f,  1.0f);
        glTexCoord2f(1.0f, 0.0f); glVertex3f( 1.0f, -1.0f,  1.0f);
        glTexCoord2f(1.0f, 1.0f); glVertex3f( 1.0f,  1.0f,  1.0f);
        glTexCoord2f(0.0f, 1.0f); glVertex3f(-1.0f,  1.0f,  1.0f);

        // Back Face
        glTexCoord2f(1.0f, 0.0f); glVertex3f(-1.0f, -1.0f, -1.0f);
        glTexCoord2f(1.0f, 1.0f); glVertex3f(-1.0f,  1.0f, -1.0f);
        glTexCoord2f(0.0f, 1.0f); glVertex3f( 1.0f,  1.0f, -1.0f);
        glTexCoord2f(0.0f, 0.0f); glVertex3f( 1.0f, -1.0f, -1.0f);
        // Top Face
        glTexCoord2f(0.0f, 1.0f); glVertex3f(-1.0f,  1.0f, -1.0f);
        glTexCoord2f(0.0f, 0.0f); glVertex3f(-1.0f,  1.0f,  1.0f);
        glTexCoord2f(1.0f, 0.0f); glVertex3f( 1.0f,  1.0f,  1.0f);
        glTexCoord2f(1.0f, 1.0f); glVertex3f( 1.0f,  1.0f, -1.0f);
        // Bottom Face
        glTexCoord2f(1.0f, 1.0f); glVertex3f(-1.0f, -1.0f, -1.0f);
        glTexCoord2f(0.0f, 1.0f); glVertex3f( 1.0f, -1.0f, -1.0f);
        glTexCoord2f(0.0f, 0.0f); glVertex3f( 1.0f, -1.0f,  1.0f);
        glTexCoord2f(1.0f, 0.0f); glVertex3f(-1.0f, -1.0f,  1.0f);
        // Right face
        glTexCoord2f(1.0f, 0.0f); glVertex3f( 1.0f, -1.0f, -1.0f);
        glTexCoord2f(1.0f, 1.0f); glVertex3f( 1.0f,  1.0f, -1.0f);
        glTexCoord2f(0.0f, 1.0f); glVertex3f( 1.0f,  1.0f,  1.0f);
        glTexCoord2f(0.0f, 0.0f); glVertex3f( 1.0f, -1.0f,  1.0f);
        // Left Face
        glTexCoord2f(0.0f, 0.0f); glVertex3f(-1.0f, -1.0f, -1.0f);
        glTexCoord2f(1.0f, 0.0f); glVertex3f(-1.0f, -1.0f,  1.0f);
        glTexCoord2f(1.0f, 1.0f); glVertex3f(-1.0f,  1.0f,  1.0f);
        glTexCoord2f(0.0f, 1.0f); glVertex3f(-1.0f,  1.0f, -1.0f);
    glEnd();
    glPopMatrix();

    frame++;
    int time=glutGet(GLUT_ELAPSED_TIME);
    if (time - timebase > 1000) {
        printf("FPS:%4.2f\n", frame*1000.0/(time-timebase));
        timebase = time;        
        frame = 0;
    }

    xrot+=0.3f;
    yrot+=0.2f;
    zrot+=0.4f;
    glutSwapBuffers();
}

int main(int argc, char** argv) {
  glutInit( &argc, argv );
  glutInitDisplayMode ( GLUT_DEPTH | GLUT_DOUBLE | GLUT_RGBA ); // Display Mode
  glutInitWindowPosition (0,0);
  glutInitWindowSize(500, 500) ; 
  glutCreateWindow("Mandlebrot");
  init();
  glutDisplayFunc(display);
  glutIdleFunc(display);
  glutReshapeFunc     ( reshape );
  glutMainLoop();         
  return 0;
}

void hsvToRgb(double h, double S, double V, uint8_t* r, uint8_t* g, uint8_t* b)
{
  // ######################################################################
  // T. Nathan Mundhenk
  // mundhenk@usc.edu
  // C/C++ Macro HSV to RGB

  double H = h;
  while (H < 0) { H += 360; };
  while (H >= 360) { H -= 360; };
  double R, G, B;
  if (V <= 0)
    { R = G = B = 0; }
  else if (S <= 0)
  {
    R = G = B = V;
  }
  else
  {
    double hf = H / 60.0;
    int i = (int)floor(hf);
    double f = hf - i;
    double pv = V * (1 - S);
    double qv = V * (1 - S * f);
    double tv = V * (1 - S * (1 - f));
    switch (i)
    {

      // Red is the dominant color

      case 0:
        R = V;
        G = tv;
        B = pv;
        break;

      // Green is the dominant color

      case 1:
        R = qv;
        G = V;
        B = pv;
        break;
      case 2:
        R = pv;
        G = V;
        B = tv;
        break;

      // Blue is the dominant color

      case 3:
        R = pv;
        G = qv;
        B = V;
        break;
      case 4:
        R = tv;
        G = pv;
        B = V;
        break;

      // Red is the dominant color

      case 5:
        R = V;
        G = pv;
        B = qv;
        break;

      // Just in case we overshoot on our math by a little, we put these here. Since its a switch it won't slow us down at all to put these here.

      case 6:
        R = V;
        G = tv;
        B = pv;
        break;
      case -1:
        R = V;
        G = pv;
        B = qv;
        break;

      // The color is not defined, we should throw an error.

      default:
        //LFATAL("i Value error in Pixel conversion, Value is %d", i);
        R = G = B = V; // Just pretend its black/white
        break;
    }
  }
  *r = (uint8_t)clamp((int)(R * 255.0));
  *g = (uint8_t)clamp((int)(G * 255.0));
  *b = (uint8_t)clamp((int)(B * 255.0));
}

