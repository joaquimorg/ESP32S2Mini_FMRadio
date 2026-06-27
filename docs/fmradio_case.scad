// =====================================================================
//  Caixa 3D para Radio FM ESP32-S2 Mini (SI4703)  -- v2 "PRO"
//  Projeto: github.com/joaquimorg/ESP32S2Mini_FMRadio
//
//  Acabamento mais cuidado, IMPRESSAO SEM SUPORTES:
//   - bordos chanfrados (bisel a 45 graus)
//   - ecra com moldura/bisel encastrado
//   - grelhas em ranhuras verticais (visual moderno)
//   - molduras gravadas a volta do LCD e dos altifalantes
//   - etiqueta gravada ("FM RADIO")
//
//  Orientacao de impressao:
//   - faceplate : face EXTERIOR para baixo (lisa); postes apontam p/ cima
//   - body      : abertura para cima
//
//  Render por peca:
//   openscad -D 'part="body"'      -o body.stl      fmradio_case.scad
//   openscad -D 'part="faceplate"' -o faceplate.stl fmradio_case.scad
// =====================================================================

part = "preview";     // "body" | "faceplate" | "preview"
$fn = 48;
eps = 0.02;

// ---------------------------------------------------------------------
//  PARAMETROS GERAIS  (mm)
// ---------------------------------------------------------------------
wall       = 2.4;
floor_t    = 2.4;
face_t     = 3.0;
corner_r   = 4.0;
interior_d = 32;

edge_margin = 7;
tb_margin   = 9;
gap_spk_lcd = 1;

// chanfros (bisel) -- todos a 45 graus => imprimem sem suporte
face_chamf  = 1.8;
body_base_chamf = 1.6;
body_top_chamf  = 1.2;
open_chamf  = 1.0;

// gravacoes
engrave_d   = 0.6;
engrave_w   = 1.0;

// ---------------------------------------------------------------------
//  ALTIFALANTES (2x 70 x 30, na vertical) -- grelha em ranhuras
// ---------------------------------------------------------------------
spk_long  = 70;
spk_short = 30;
spk_recess_depth = 1.6;
grille_margin   = 6;
grille_slots    = 5;
grille_slot_w   = 2.4;
grille_frame_gap = 3.5;

// ---------------------------------------------------------------------
//  LCD ST7789 76x284  --  medidas REAIS da folha (GoldenMorning 8 pinos)
//
//  Em PAISAGEM (comprimento na horizontal):
//    PCB 73.15 x 21.08 | LCM 62.5 x 17.9 | LCD 60.55 x 17 |
//    POL 57.25 x 16.7 | Area activa 55.295 x 14.797
//  A area activa esta centrada no vidro; o PCB tem ~3.78 mm de desvio
//  (fila de 8 pinos num topo, 2 furos Ø2.5 no outro).
//  >> ORIENTACAO ASSUMIDA: pinos do lado DIREITO (+X) <<
//     (se montares com os pinos a esquerda: lcd_aa_off_x = -3.78 e lcd_fpc_side = 2)
// ---------------------------------------------------------------------
lcd_mod_w     = 73.15;  // contorno PCB (dim. MAIOR, paisagem)
lcd_mod_h     = 21.08;  // contorno PCB (dim. MENOR)
lcd_mod_clear = 0.5;    // folga do encaixe, por lado
lcd_frame_h   = 3.5;    // altura das paredes de retencao (para dentro)
lcd_frame_wall= 1.6;    // espessura das paredes de retencao

lcd_aa_w = 55.295;      // area ACTIVA visivel (dim. maior)
lcd_aa_h = 14.797;      // area ACTIVA visivel (dim. menor)
lcd_aa_off_x = 3.78;    // desvio da area activa vs centro do PCB (pinos a +X)
lcd_aa_off_y = 0;       // (centrada no eixo curto)
lcd_win_margin = 0.6;   // janela = area activa + esta folga

lcd_fpc_w    = 20;      // folga p/ a fila de 8 pinos / fios (no topo dos pinos)
lcd_fpc_side = 3;       // lado dos pinos: 0=baixo 1=cima 2=esq 3=dir

lcd_offset_y = 9;       // posicao vertical do ecra na frente
lcd_frame_gap = 3.5;    // moldura gravada a volta da janela

// largura central reservada (mantem o ECRA centrado, contendo o PCB desviado)
center_w = lcd_mod_w + 2*lcd_mod_clear + 2*lcd_frame_wall + 2*abs(lcd_aa_off_x);

// ---------------------------------------------------------------------
//  MEMBRANA 1x4 (cola por fora) -- passagem da fita E DA FICHA
//  A abertura tem de deixar passar o conetor (mede a tua ficha!).
// ---------------------------------------------------------------------
slot_gap_from_lcd = 25;
cable_slot_w = 15;     // largura da abertura (>= largura da ficha)
cable_slot_h = 4.5;    // altura da abertura (>= espessura da ficha)
cable_slot_r = 1.0;

// ---------------------------------------------------------------------
//  ETIQUETA gravada
// ---------------------------------------------------------------------
label_text = "FM RADIO";
label_font = "Poppins:style=Bold";
label_size = 6.0;
label_y_above_lcd = 6.5;

// ---------------------------------------------------------------------
//  FIXACAO (4 postes nos cantos, parafuso M3 pela traseira)
// ---------------------------------------------------------------------
post_r        = 3.0;
post_inset    = 7.5;
post_pilot_d  = 2.5;
screw_clear_d = 3.4;
screw_head_d  = 6.5;
screw_head_h  = 2.2;

// ---------------------------------------------------------------------
//  ABERTURAS laterais
// ---------------------------------------------------------------------
usb_w = 10; usb_h = 4.0; usb_z = 9;
ant_jack = true; ant_d = 6.5; ant_y = 20;

// ---------------------------------------------------------------------
//  DIMENSOES EXTERIORES (calculadas)
// ---------------------------------------------------------------------
interior_w = edge_margin + spk_short + gap_spk_lcd + center_w
             + gap_spk_lcd + spk_short + edge_margin;
interior_h = tb_margin + spk_long + tb_margin;
W = interior_w + 2*wall;
H = interior_h + 2*wall;
D = floor_t + interior_d + face_t;

spk_l_x = wall + edge_margin + spk_short/2;
spk_r_x = W - spk_l_x;
spk_cy  = H/2;
cen_x   = W/2;
lcd_cy  = H/2 + lcd_offset_y;
lcd_win_h_eff = lcd_aa_h + 2*lcd_win_margin;   // altura efectiva da janela (area activa)
lcd_win_w_eff = lcd_aa_w + 2*lcd_win_margin;   // largura efectiva da janela
mem_cy  = lcd_cy - lcd_win_h_eff/2 - slot_gap_from_lcd;

echo(str(">>> EXTERIOR: ", W, " x ", H, " x ", D, " mm"));

// =====================================================================
//  HELPERS
// =====================================================================
module rrect(w, h, r) { offset(r=r) offset(delta=-r) square([w,h], center=true); }

module cham_prism(w, h, d, r, cb, ct) {
    if (cb > 0)
        hull() {
            linear_extrude(eps) rrect(w-2*cb, h-2*cb, max(r-cb,0.5));
            translate([0,0,cb]) linear_extrude(eps) rrect(w, h, r);
        }
    translate([0,0,cb]) linear_extrude(d - cb - ct) rrect(w, h, r);
    if (ct > 0)
        hull() {
            translate([0,0,d-ct]) linear_extrude(eps) rrect(w, h, r);
            translate([0,0,d-eps]) linear_extrude(eps) rrect(w-2*ct, h-2*ct, max(r-ct,0.5));
        }
}

module rect_cut_chamf(cx, cy, w, h, rr, ch, thru) {
    translate([cx, cy, 0]) {
        hull() {
            translate([0,0,-eps]) linear_extrude(eps) rrect(w+2*ch, h+2*ch, rr+ch);
            translate([0,0,ch])   linear_extrude(eps) rrect(w, h, rr);
        }
        translate([0,0,ch-eps]) linear_extrude(thru) rrect(w, h, rr);
    }
}

module engrave_frame(cx, cy, w, h, r) {
    translate([cx, cy, -eps]) linear_extrude(engrave_d+eps)
        difference() {
            rrect(w+engrave_w, h+engrave_w, r);
            rrect(w-engrave_w, h-engrave_w, max(r-engrave_w,0.4));
        }
}

// ranhura vertical em capsula (hull de 2 circulos) com boca chanfrada
module vslot_cut(cx, cy, w, len, ch, thru) {
    module cap(extra) {
        hull() {
            translate([0,  (len-w)/2, 0]) circle(d = w + 2*extra, $fn = 28);
            translate([0, -(len-w)/2, 0]) circle(d = w + 2*extra, $fn = 28);
        }
    }
    translate([cx, cy, 0]) {
        hull() {
            translate([0,0,-eps]) linear_extrude(eps) cap(ch);
            translate([0,0,ch])   linear_extrude(eps) cap(0);
        }
        translate([0,0,ch-eps]) linear_extrude(thru) cap(0);
    }
}

module grille_slots_cut(cx, cy) {
    aw = spk_short - 2*grille_margin;
    sl = spk_long  - 2*grille_margin;
    n  = grille_slots;
    px = aw / n;
    for (i = [0:n-1])
        vslot_cut(cx - aw/2 + px/2 + i*px, cy, grille_slot_w, sl, open_chamf*0.6, face_t);
}

module speaker_pocket(cx, cy) {
    translate([cx, cy, face_t - spk_recess_depth])
        linear_extrude(spk_recess_depth + 1)
            square([spk_short + 1, spk_long + 1], center=true);
}

// -- LCD: paredes de retencao com o contorno do modulo (registo) --
//    centro do encaixe = centro da janela menos o offset da area activa
function lcd_pkt_cx() = cen_x   - lcd_aa_off_x;
function lcd_pkt_cy() = lcd_cy  - lcd_aa_off_y;

module lcd_frame_add() {
    fo_w = lcd_mod_w + 2*lcd_mod_clear + 2*lcd_frame_wall;
    fo_h = lcd_mod_h + 2*lcd_mod_clear + 2*lcd_frame_wall;
    fi_w = lcd_mod_w + 2*lcd_mod_clear;
    fi_h = lcd_mod_h + 2*lcd_mod_clear;
    translate([lcd_pkt_cx(), lcd_pkt_cy(), face_t])
        linear_extrude(lcd_frame_h)
            difference() {
                rrect(fo_w, fo_h, lcd_frame_wall + 0.6);
                rrect(fi_w, fi_h, 0.8);
            }
}

// -- folga na parede de retencao p/ a flat-flex / conector sair --
module lcd_fpc_cut() {
    fi_w = lcd_mod_w + 2*lcd_mod_clear;
    fi_h = lcd_mod_h + 2*lcd_mod_clear;
    t = lcd_frame_wall * 3;
    zc = face_t + lcd_frame_h/2 + 0.5;
    zs = lcd_frame_h + 2;
    cx = lcd_pkt_cx();  cy = lcd_pkt_cy();
    if (lcd_fpc_side == 0)        // baixo (-Y)
        translate([cx, cy - fi_h/2 - lcd_frame_wall/2, zc]) cube([lcd_fpc_w, t, zs], center=true);
    else if (lcd_fpc_side == 1)   // cima (+Y)
        translate([cx, cy + fi_h/2 + lcd_frame_wall/2, zc]) cube([lcd_fpc_w, t, zs], center=true);
    else if (lcd_fpc_side == 2)   // esquerda (-X)
        translate([cx - fi_w/2 - lcd_frame_wall/2, cy, zc]) cube([t, lcd_fpc_w, zs], center=true);
    else                          // direita (+X)
        translate([cx + fi_w/2 + lcd_frame_wall/2, cy, zc]) cube([t, lcd_fpc_w, zs], center=true);
}

// =====================================================================
//  FACEPLATE (face exterior em z=0)
// =====================================================================
module faceplate() {
    difference() {
        union() {
            translate([W/2, H/2, 0]) cham_prism(W, H, face_t, corner_r, face_chamf, 0);
            for (sx = [post_inset, W-post_inset])
                for (sy = [post_inset, H-post_inset])
                    translate([sx, sy, face_t]) cylinder(h = interior_d-0.5, r = post_r);
            lcd_frame_add();                       // paredes de retencao do LCD
        }

        // -- janela da area activa (passante, com boca chanfrada) --
        rect_cut_chamf(cen_x, lcd_cy, lcd_win_w_eff, lcd_win_h_eff, 1.2, open_chamf, face_t);
        lcd_fpc_cut();                             // folga p/ flat-flex / conector

        grille_slots_cut(spk_l_x, spk_cy);
        grille_slots_cut(spk_r_x, spk_cy);
        speaker_pocket(spk_l_x, spk_cy);
        speaker_pocket(spk_r_x, spk_cy);

        rect_cut_chamf(cen_x, mem_cy, cable_slot_w, cable_slot_h, cable_slot_r, open_chamf*0.6, face_t);

        engrave_frame(cen_x, lcd_cy, lcd_win_w_eff + 2*lcd_frame_gap, lcd_win_h_eff + 2*lcd_frame_gap, 3);
        engrave_frame(spk_l_x, spk_cy, spk_short + 2*grille_frame_gap, spk_long + 2*grille_frame_gap, 3);
        engrave_frame(spk_r_x, spk_cy, spk_short + 2*grille_frame_gap, spk_long + 2*grille_frame_gap, 3);

        if (label_text != "")
            translate([cen_x, lcd_cy + lcd_win_h_eff/2 + label_y_above_lcd, -eps])
                linear_extrude(engrave_d + eps)
                    mirror([1,0,0])
                        text(label_text, size = label_size, font = label_font,
                             halign = "center", valign = "center");

        for (sx = [post_inset, W-post_inset])
            for (sy = [post_inset, H-post_inset])
                translate([sx, sy, face_t+8]) cylinder(h = interior_d, d = post_pilot_d);
    }
}

// =====================================================================
//  BODY (abertura para cima; fundo em z=0)
// =====================================================================
module body() {
    body_h = floor_t + interior_d;
    difference() {
        translate([W/2, H/2, 0]) cham_prism(W, H, body_h, corner_r, body_base_chamf, body_top_chamf);

        translate([W/2, H/2, floor_t])
            cham_prism(interior_w, interior_h, interior_d+1, max(corner_r-wall,0.5), 0, 0);

        translate([cen_x, wall/2, floor_t + usb_z])
            cube([usb_w, wall*3, usb_h], center=true);

        if (ant_jack)
            translate([W - wall/2, ant_y, floor_t + interior_d/2])
                rotate([0,90,0]) cylinder(h = wall*3, d = ant_d, center=true);

        for (sx = [post_inset, W-post_inset])
            for (sy = [post_inset, H-post_inset]) {
                translate([sx, sy, -1]) cylinder(h = floor_t+2, d = screw_clear_d);
                translate([sx, sy, -0.01]) cylinder(h = screw_head_h, d = screw_head_d);
            }
    }
}

// =====================================================================
//  RENDER
// =====================================================================
if (part == "body") body();
else if (part == "faceplate") faceplate();
else if (part == "plate") {            // chapa de impressao: as 2 pecas lado a lado
    body();                            // fundo no z=0 (abertura p/ cima)
    translate([W + 12, 0, 0]) faceplate();  // face exterior no z=0 (postes p/ cima)
}
else if (part == "hero") {
    color([0.80,0.80,0.82]) body();
    translate([W, 0, floor_t + interior_d + 20 + face_t])
        rotate([0,180,0]) color([0.20,0.22,0.26]) faceplate();
}
else {
    color([0.78,0.78,0.80]) body();
    explode = interior_d + 20;
    color([0.20,0.22,0.26])
        translate([0,0, floor_t + explode])
            translate([0,0,face_t]) mirror([0,0,1]) faceplate();
}
