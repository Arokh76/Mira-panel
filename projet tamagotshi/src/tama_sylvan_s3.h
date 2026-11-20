#pragma once
// Tamagotchi S3 V8.3.3 — Sylvan dessine en code, sans PNG ni RLE.
// Toutes les coordonnees sont dans le framebuffer natif 170x320.
// Aucun fond blanc, aucune interpolation, aucun sprite recadre.
// Utilise dans main.cpp uniquement sous #ifdef TAMA_S3_LCD.
namespace TamaSylvanS3 {

static inline uint16_t C(uint8_t r,uint8_t g,uint8_t b) {
  return rgb565(r,g,b);
}
static const uint16_t UNUSED_SENTINEL = 0; // aucune allocation graphique

// Feuille composee de bandes arrondies, sans polygone carre ni halo PNG.
// Les 11 profils dessinent deux courbes de tapering vers la pointe.
// L'outline est peint d'abord, puis le vert interieur (aucun anti-alias).
static inline void leaf(int bx,int by,int tx,int ty,int width,
                        uint16_t edge,uint16_t green,uint16_t shine) {
  const int dx=tx-bx,dy=ty-by;
  const int len=max(1,max(abs(dx),abs(dy)));
  static const uint8_t widths[11]={0,4,7,9,10,10,9,7,5,2,0};
  for(int pass=0;pass<2;++pass) {
    const int w=pass==0?width:max(2,width-3);
    const uint16_t color=pass==0?edge:green;
    int lx=bx,ly=by,rx=bx,ry=by;
    for(int i=1;i<=10;++i) {
      const int centerX=bx+dx*i/10;
      const int centerY=by+dy*i/10;
      const int r=w*widths[i]/10;
      const int ox=-dy*r/len,oy=dx*r/len;
      const int nx=centerX+ox,ny=centerY+oy;
      const int px=centerX-ox,py=centerY-oy;
      gfx.fillTriangle(lx,ly,rx,ry,nx,ny,color);
      gfx.fillTriangle(rx,ry,nx,ny,px,py,color);
      lx=nx;ly=ny;rx=px;ry=py;
    }
  }
  gfx.drawLine(bx,by,tx-dx/15,ty-dy/15,shine);
  gfx.drawLine(bx+dx*5/10,by+dy*5/10,
               bx+dx*8/10-dy/18,by+dy*8/10+dx/18,shine);
}
static inline void heartShape(int x,int y,int s,uint16_t color) {
  gfx.fillCircle(x-s/2,y,s/2,color);
  gfx.fillCircle(x+s/2,y,s/2,color);
  gfx.fillTriangle(x-s,y,x+s,y,x,y+s+3,color);
}
static inline void eye(int x,int y,bool closed,bool happy,bool surprised) {
  const uint16_t ink=C(16,47,43), iris=C(37,137,91), glint=C(251,255,248);
  if(closed || happy) {
    gfx.drawLine(x-7,y,x-3,y+3,ink);
    gfx.drawLine(x-3,y+3,x+3,y+3,ink);
    gfx.drawLine(x+3,y+3,x+7,y,ink);
    gfx.drawPixel(x-6,y+1,ink);
    return;
  }
  gfx.fillEllipse(x,y,8,12,ink);
  gfx.fillEllipse(x+1,y+1,5,9,iris);
  gfx.fillEllipse(x+2,y-1,3,6,C(6,58,54));
  gfx.fillCircle(x-2,y-5,3,glint);
  gfx.fillCircle(x+4,y+5,1,glint);
  if(surprised) gfx.drawFastHLine(x-5,y-17,10,ink);
}
static inline void paw(int x,int y,bool lifted,uint16_t outline,uint16_t cream) {
  const int py=y-(lifted?12:0);
  // Petites pattes ovales au lieu de grandes boules detachees du ventre.
  gfx.fillEllipse(x,py,9,10,outline);
  gfx.fillEllipse(x,py-1,7,8,cream);
  if(!lifted) {
    gfx.drawFastVLine(x-3,py+5,4,C(176,163,124));
    gfx.drawFastVLine(x+3,py+5,4,C(176,163,124));
  } else {
    gfx.fillCircle(x-2,py-1,2,C(244,167,176));
    gfx.fillCircle(x+3,py-1,2,C(244,167,176));
  }
}

static inline void draw(uint32_t now) {
  // Animations 95 ms (~10 images/s) : seules les petites parties mobiles
  // sont recalculees, le framebuffer est reconstruit proprement a chaque frame.
  const bool active=effect!=FX_NONE && (int32_t)(now-effectUntil)<0;
  const Effect action=active?effect:FX_NONE;
  const uint32_t t=active?(uint32_t)(now-effectStarted):0U;
  const bool sleeping=pet.dort;
  const bool eating=action==FX_FOOD;
  const bool washing=action==FX_CLEAN;
  const bool cuddling=action==FX_HUG;
  const bool playing=action==FX_PLAY;
  const bool gift=action==FX_GIFT;
  const bool cheering=action==FX_GROW || action==FX_WAKE ||
                       action==FX_HEAL || action==FX_SURPRISE;
  const int phase=(int)((now/260UL)%8UL);
  const int breathe=(phase<4?phase:7-phase); // 0,1,2,3,3,2,1,0
  const int sway=(int)((now/430UL)%6UL)-3;
  const bool blink=(now%3900UL)<150UL;
  const bool chew=eating && t>=1150U && t<2800U;
  const bool eyesShut=sleeping || washing || cuddling || (chew && (t/260UL)%2U);
  const bool happy=(cuddling || cheering ||
                    (eating && t>2750U) ||
                    (playing && (t/380UL)%2U));
  const bool surprised=gift && t<850UL;
  const uint16_t outline=C(16,55,47);
  const uint16_t deep=C(31,110,59);
  const uint16_t green=C(103,214,69);
  const uint16_t lime=C(186,242,87);
  const uint16_t lightLeaf=C(210,250,88);
  const uint16_t cream=C(251,247,209);
  const uint16_t highlight=C(255,255,232);
  const uint16_t blush=C(247,151,173);
  const uint16_t groundShadow=C(65,119,97);

  int walk=0;
  if(!sleeping && isWalking(now)) walk=((now/230UL)%2U)?3:-3;
  const int run=playing?((int)((t/230UL)%5UL)-2)*2:0;
  const int x=85+walk+run;
  // La reference du sol reste TOUJOURS y=236. Les pattes ne peuvent pas
  // passer sous le bandeau de nom y=255.
  gfx.fillEllipse(x,240,46,5,groundShadow);

  if(sleeping) {
    // Silhouette horizontale COMPLETE, pas un personnage assis yeux fermes.
    gfx.fillEllipse(x+11,220,48,19,outline);
    gfx.fillEllipse(x+11,218,45,17,green);
    gfx.fillEllipse(x+8,225,24,8,cream); // ventre du dormeur
    leaf(x+30,213,x+57,191,11,outline,green,lime);
    leaf(x+23,210,x+47,196,9,outline,lime,lightLeaf);
    gfx.fillEllipse(x+37,232,15,7,outline);
    gfx.fillEllipse(x+37,230,12,6,cream);
    gfx.fillEllipse(x-23,217,29,22,outline);
    gfx.fillEllipse(x-24,216,26,19,cream);
    leaf(x-40,205,x-56,185,10,outline,green,lime);
    leaf(x-17,201,x-11,177,11,outline,lime,lightLeaf);
    gfx.fillEllipse(x-36,224,10,6,blush);
    gfx.drawLine(x-36,216,x-30,219,outline);
    gfx.drawLine(x-30,219,x-23,215,outline);
    gfx.drawLine(x-14,215,x-8,217,outline);
    gfx.drawLine(x-8,217,x-4,213,outline);
    gfx.drawFastHLine(x-23,226,7,outline);
    // La respiration se voit sur le flanc et la queue.
    gfx.drawFastHLine(x+5,219+breathe/2,19,C(207,245,151));
    return;
  }

  const int bob=(playing?((t/140UL)%2U?0:-4):0);
  const int tilt=(eating && t>=700UL && t<2650UL)?4:0;
  const int headY=157+breathe/2+bob+tilt;
  const int bodyY=205+breathe/2+bob;

  // Queue et feuillage derriere le corps : mouvements par articulation.
  gfx.fillEllipse(x-42,bodyY-5,22,23,outline);
  gfx.fillEllipse(x-44,bodyY-7,19,20,green);
  leaf(x-43,bodyY-11,x-64,bodyY-39+sway,15,outline,green,lime);
  leaf(x-43,bodyY-5,x-60,bodyY-22-sway,13,outline,lime,lightLeaf);
  // Membres arriere : ne sont jamais masques par le nom.
  paw(x-23,229+bob,false,outline,cream);
  paw(x+23,229+bob,false,outline,cream);
  // Corps rond, ombre en bas et ventre lumineux.
  gfx.fillEllipse(x,bodyY,34,35,outline);
  gfx.fillEllipse(x,bodyY-2,31,32,green);
  gfx.fillEllipse(x+4,bodyY+11,24,19,deep);
  gfx.fillEllipse(x,bodyY+7,23,22,cream);
  gfx.fillEllipse(x-8,bodyY+3,10,12,highlight);

  // Branches de feuilles / oreilles : Sylvan adulte A.
  // Pointes absolues < 155 pixels. Les articulations suivent le headY.
  leaf(x-27,headY-19,x-56,headY-49+sway,20,outline,deep,green);
  leaf(x-32,headY-9,x-57,headY-20-sway,17,outline,green,lime);
  leaf(x+27,headY-17,x+55,headY-47-sway,20,outline,deep,green);
  leaf(x+32,headY-7,x+58,headY-16+sway,17,outline,green,lime);
  leaf(x-11,headY-31,x-31,headY-65+sway,14,outline,green,lime);
  leaf(x+12,headY-33,x+34,headY-67-sway,15,outline,green,lime);
  // Petite couronne centrale, marquee et reconnaissable.
  leaf(x,headY-24,x+sway,headY-77,18,outline,lime,lightLeaf);

  // Vraie tete avec masque creme : aucune bordure transparente/anti-alias.
  gfx.fillEllipse(x,headY,42,39,outline);
  gfx.fillEllipse(x,headY-1,39,36,green);
  gfx.fillEllipse(x,headY+7,36,29,cream);
  // Frange en feuille, suit le mouvement de la tete.
  gfx.fillTriangle(x-26,headY-15,x-8,headY-29,x-3,headY-17,green);
  gfx.fillTriangle(x+3,headY-18,x+11,headY-30,x+27,headY-15,green);
  // Yeux et joues.
  eye(x-16,headY+3,eyesShut || blink,happy,surprised);
  eye(x+16,headY+3,eyesShut || blink,happy,surprised);
  gfx.fillEllipse(x-30,headY+17,9,5,blush);
  gfx.fillEllipse(x+30,headY+17,9,5,blush);
  if(chew) {
    if((t/230UL)%2U) {
      gfx.fillEllipse(x,headY+22,7,5,outline);
      gfx.fillEllipse(x,headY+24,4,2,C(250,124,149));
    } else {
      gfx.drawFastHLine(x-6,headY+22,12,outline);
    }
  } else if(surprised || (eating && t>520U && t<1150U)) {
    gfx.fillEllipse(x,headY+22,6,8,outline);
    gfx.fillEllipse(x,headY+25,3,3,blush);
  } else if(happy) {
    gfx.drawLine(x-8,headY+20,x,headY+26,outline);
    gfx.drawLine(x,headY+26,x+8,headY+20,outline);
    gfx.drawLine(x-4,headY+23,x+4,headY+23,C(218,116,130));
  } else {
    gfx.drawLine(x-5,headY+20,x,headY+23,outline);
    gfx.drawLine(x,headY+23,x+5,headY+20,outline);
  }
  // Collier sous le menton : y~198, jamais a la hauteur de la bouche (~179).
  // V8.3.3 dessinait ces feuilles SUR le visage, creant un rectangle vert.
  leaf(x-4,bodyY-3,x-21,bodyY-12,8,outline,deep,lime);
  leaf(x+4,bodyY-3,x+21,bodyY-12,8,outline,green,lightLeaf);
  gfx.fillCircle(x,bodyY-2,5,outline);
  gfx.fillCircle(x,bodyY-3,3,lime);

  // Bras / pattes AVANT : articulation pilotee selon l'activite.
  const bool reaching=eating && t>=550UL && t<2550UL;
  const bool raised=cuddling || gift || cheering;
  if(reaching) {
    paw(x-24,bodyY+12,false,outline,cream);
    paw(x+28,bodyY+5,true,outline,cream);
  } else if(raised) {
    paw(x-30,bodyY+9,true,outline,cream);
    paw(x+30,bodyY+9,true,outline,cream);
  } else if(playing) {
    paw(x-26,bodyY+12,false,outline,cream);
    paw(x+32,bodyY+13,(t/230UL)%2U,outline,cream);
  } else {
    paw(x-23,bodyY+21,false,outline,cream);
    paw(x+23,bodyY+21,false,outline,cream);
  }

  if(eating) {
    // Une gamelle stable, et une vraie bouche qui mache (ci-dessus).
    const int lift=t<500UL?(int)((500UL-t)/100UL):0;
    const int bx=x+43, by=231+lift;
    gfx.fillEllipse(bx,by-12,23,7,outline);
    gfx.fillEllipse(bx,by-15,20,5,C(254,235,177));
    for(int i=0;i<5;++i) {
      gfx.fillCircle(bx-14+i*7,by-17-(i%2)*3,4,C(188,117,65));
      gfx.fillCircle(bx-14+i*7,by-18-(i%2)*3,2,C(244,190,98));
    }
    gfx.fillRoundRect(bx-21,by-11,42,13,4,outline);
    gfx.fillRoundRect(bx-18,by-11,36,11,4,C(84,191,84));
    gfx.drawFastHLine(bx-13,by-7,26,lime);
    // Morceau de nourriture qui rejoint la bouche en suivant une trajectoire
    // visible, avec le bras deja leve. Le bol reste pose au sol.
    if(t>=950UL && t<1450UL) {
      const int step=(int)(t-950UL);
      const int fx=bx-7-(step*33)/500;
      const int fy=by-20-(step*29)/500;
      gfx.fillCircle(fx,fy,5,C(139,83,46));
      gfx.fillCircle(fx-1,fy-1,3,C(238,178,81));
    }
  }
  if(playing) {
    const int bx=x+48+(int)((t/300UL)%3UL)-1;
    const int bounce=abs((int)((t/170UL)%8UL)-4);
    const int by=228-bounce*5;
    gfx.fillCircle(bx,by,11,outline);
    gfx.fillCircle(bx,by,9,C(231,229,63));
    gfx.fillCircle(bx-3,by-3,4,lime);
    gfx.fillCircle(bx+4,by+2,3,deep);
  }
  if(cuddling) {
    heartShape(x,bodyY+9,13,C(248,105,154));
    heartShape(x-45,155-(int)((t/75UL)%52UL),6,C(255,155,183));
    heartShape(x+48,160-(int)((t/92UL)%48UL),7,C(255,93,151));
  }
  if(gift) {
    const int giftX=x+48;
    const int lift=min(22,(int)(t/56UL));
    gfx.fillRoundRect(giftX-19,210,38,23,4,outline);
    gfx.fillRoundRect(giftX-17,209,34,22,4,C(243,137,154));
    gfx.fillRect(giftX-3,211,6,20,C(255,216,95));
    gfx.fillRoundRect(giftX-21,203-lift,42,9,3,outline);
    gfx.fillRoundRect(giftX-19,203-lift,38,7,3,C(255,216,95));
    if(t>630UL) {
      heartShape(giftX,174-(int)((t/120UL)%18UL),6,C(246,110,156));
    }
  }
  if(washing) {
    // Douche avec gouttes animees et mousse, DESSINEES en coordonnees du
    // personnage. Pas de photos decoupees ni de couche blanche parasite.
    const uint16_t metal=C(184,214,221), water=C(70,203,248);
    gfx.fillRoundRect(18,68,9,30,3,metal);
    gfx.fillRoundRect(24,68,55,9,4,metal);
    gfx.fillRoundRect(65,73,26,12,4,C(123,175,189));
    for(int i=0;i<8;++i) {
      const int px=67+i*7;
      const int py=88+(int)((t/9UL+i*23UL)%102UL);
      gfx.drawFastVLine(px,py,7,water);
      gfx.drawPixel(px+1,py+1,TFT_WHITE);
    }
    for(int i=0;i<11;++i) {
      const int bx=x-29+(i*17)%59;
      const int by=230-(int)((t/29UL+i*13UL)%69UL);
      const int rad=3+i%3;
      gfx.drawCircle(bx,by,rad,C(212,245,252));
      gfx.drawPixel(bx-rad/2,by-rad/2,TFT_WHITE);
    }
    gfx.fillEllipse(x-15,227,19,8,C(213,245,249));
    gfx.fillEllipse(x+17,230,22,7,C(179,232,242));
  }
  if(cheering) {
    const int rise=(int)((t/45UL)%87UL);
    gfx.drawLine(x-51,181-rise,x-51,191-rise,lime);
    gfx.drawLine(x-56,186-rise,x-46,186-rise,lime);
    gfx.drawLine(x+52,177-rise/2,x+52,188-rise/2,TFT_YELLOW);
    gfx.drawLine(x+47,183-rise/2,x+57,183-rise/2,TFT_YELLOW);
  }
}

} // namespace TamaSylvanS3
