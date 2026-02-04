/**
 * GBA Emulator Core - PPU (Pixel Processing Unit) Implementation
 */

#include <string.h>
#include "gba_internal.h"

void ppu_init(gba_t *gba)
{
    memset(&gba->ppu, 0, sizeof(ppu_state_t));
}

void ppu_reset(gba_t *gba)
{
    memset(&gba->ppu, 0, sizeof(ppu_state_t));
    gba->ppu.dispcnt = 0x0080;  // Forced blank
}

// Get pixel from tile
static inline uint8_t get_tile_pixel(gba_t *gba, uint32_t tile_base, uint32_t tile_num,
                                      int x, int y, bool is_8bpp)
{
    if (is_8bpp) {
        uint32_t addr = tile_base + tile_num * 64 + y * 8 + x;
        return gba->mem.vram[addr];
    } else {
        uint32_t addr = tile_base + tile_num * 32 + y * 4 + (x >> 1);
        uint8_t byte = gba->mem.vram[addr];
        return (x & 1) ? (byte >> 4) : (byte & 0xF);
    }
}

// Render a text mode background scanline
static void render_bg_text(gba_t *gba, int bg, uint16_t *line)
{
    uint16_t bgcnt = gba->ppu.bgcnt[bg];
    int tile_base = ((bgcnt >> 2) & 3) * 0x4000;
    int map_base = ((bgcnt >> 8) & 0x1F) * 0x800;
    bool is_8bpp = bgcnt & (1 << 7);
    int screen_size = (bgcnt >> 14) & 3;

    int scroll_x = gba->ppu.bghofs[bg];
    int scroll_y = (gba->ppu.bgvofs[bg] + gba->ppu.vcount) & 0x1FF;

    int map_width = (screen_size & 1) ? 512 : 256;
    int map_height = (screen_size & 2) ? 512 : 256;

    int tile_y = (scroll_y / 8) & 31;
    int fine_y = scroll_y & 7;

    for (int x = 0; x < 240; x++) {
        int screen_x = (scroll_x + x) & (map_width - 1);
        int tile_x = (screen_x / 8) & 31;
        int fine_x = screen_x & 7;

        // Calculate map offset
        int map_offset = map_base;
        if (screen_x >= 256 && (screen_size & 1)) {
            map_offset += 0x800;
        }
        if (scroll_y >= 256 && (screen_size & 2)) {
            map_offset += (screen_size & 1) ? 0x1000 : 0x800;
        }

        // Read map entry
        uint16_t entry = *(uint16_t*)&gba->mem.vram[map_offset + (tile_y * 32 + tile_x) * 2];
        int tile_num = entry & 0x3FF;
        int palette = (entry >> 12) & 0xF;
        bool hflip = entry & (1 << 10);
        bool vflip = entry & (1 << 11);

        int px = hflip ? (7 - fine_x) : fine_x;
        int py = vflip ? (7 - fine_y) : fine_y;

        uint8_t color_idx = get_tile_pixel(gba, tile_base, tile_num, px, py, is_8bpp);

        if (color_idx != 0) {
            uint32_t palette_addr;
            if (is_8bpp) {
                palette_addr = color_idx * 2;
            } else {
                palette_addr = (palette * 16 + color_idx) * 2;
            }
            line[x] = *(uint16_t*)&gba->mem.palette[palette_addr];
        }
    }
}

// Render an affine background scanline
static void render_bg_affine(gba_t *gba, int bg, uint16_t *line)
{
    int idx = bg - 2;  // BG2 = 0, BG3 = 1
    uint16_t bgcnt = gba->ppu.bgcnt[bg];
    int tile_base = ((bgcnt >> 2) & 3) * 0x4000;
    int map_base = ((bgcnt >> 8) & 0x1F) * 0x800;
    int screen_size = (bgcnt >> 14) & 3;
    bool wrap = bgcnt & (1 << 13);

    int size = 128 << screen_size;

    int32_t ref_x = gba->ppu.bgx[idx];
    int32_t ref_y = gba->ppu.bgy[idx];
    int16_t pa = gba->ppu.bgpa[idx];
    int16_t pc = gba->ppu.bgpc[idx];

    for (int x = 0; x < 240; x++) {
        int32_t tex_x = ref_x >> 8;
        int32_t tex_y = ref_y >> 8;

        if (wrap) {
            tex_x &= size - 1;
            tex_y &= size - 1;
        }

        if (tex_x >= 0 && tex_x < size && tex_y >= 0 && tex_y < size) {
            int tile_x = tex_x / 8;
            int tile_y = tex_y / 8;
            int fine_x = tex_x & 7;
            int fine_y = tex_y & 7;

            uint8_t tile_num = gba->mem.vram[map_base + tile_y * (size / 8) + tile_x];
            uint8_t color_idx = gba->mem.vram[tile_base + tile_num * 64 + fine_y * 8 + fine_x];

            if (color_idx != 0) {
                line[x] = *(uint16_t*)&gba->mem.palette[color_idx * 2];
            }
        }

        ref_x += pa;
        ref_y += pc;
    }
}

// Render bitmap mode 3 (240x160, 15-bit direct color)
static void render_mode3(gba_t *gba, uint16_t *line)
{
    int y = gba->ppu.vcount;
    uint16_t *vram = (uint16_t*)gba->mem.vram;

    for (int x = 0; x < 240; x++) {
        line[x] = vram[y * 240 + x];
    }
}

// Render bitmap mode 4 (240x160, 8-bit paletted, double buffered)
static void render_mode4(gba_t *gba, uint16_t *line)
{
    int y = gba->ppu.vcount;
    uint32_t base = (gba->ppu.dispcnt & (1 << 4)) ? 0xA000 : 0;

    for (int x = 0; x < 240; x++) {
        uint8_t color_idx = gba->mem.vram[base + y * 240 + x];
        if (color_idx != 0) {
            line[x] = *(uint16_t*)&gba->mem.palette[color_idx * 2];
        }
    }
}

// Render bitmap mode 5 (160x128, 15-bit, double buffered)
static void render_mode5(gba_t *gba, uint16_t *line)
{
    int y = gba->ppu.vcount;
    uint32_t base = (gba->ppu.dispcnt & (1 << 4)) ? 0xA000 : 0;

    if (y >= 128) return;

    uint16_t *vram = (uint16_t*)&gba->mem.vram[base];
    int start_x = (240 - 160) / 2;

    for (int x = 0; x < 160; x++) {
        line[start_x + x] = vram[y * 160 + x];
    }
}

// Render sprites
static void render_sprites(gba_t *gba, uint16_t *line, uint8_t *priority)
{
    uint16_t *oam = (uint16_t*)gba->mem.oam;
    int y = gba->ppu.vcount;

    // Process sprites in reverse order (lower index = higher priority)
    for (int i = 127; i >= 0; i--) {
        uint16_t attr0 = oam[i * 4 + 0];
        uint16_t attr1 = oam[i * 4 + 1];
        uint16_t attr2 = oam[i * 4 + 2];

        // Check if sprite is disabled or in affine double mode
        int obj_mode = (attr0 >> 8) & 3;
        if (obj_mode == 2) continue;  // Disabled

        bool is_affine = attr0 & (1 << 8);
        bool double_size = attr0 & (1 << 9);

        // Get sprite dimensions
        int shape = (attr0 >> 14) & 3;
        int size = (attr1 >> 14) & 3;

        static const int widths[4][4] = {
            {8, 16, 32, 64},   // Square
            {16, 32, 32, 64},  // Horizontal
            {8, 8, 16, 32},    // Vertical
            {0, 0, 0, 0}
        };
        static const int heights[4][4] = {
            {8, 16, 32, 64},   // Square
            {8, 8, 16, 32},    // Horizontal
            {16, 32, 32, 64},  // Vertical
            {0, 0, 0, 0}
        };

        int width = widths[shape][size];
        int height = heights[shape][size];
        int display_width = double_size ? width * 2 : width;
        int display_height = double_size ? height * 2 : height;

        // Get sprite position
        int sprite_y = attr0 & 0xFF;
        int sprite_x = attr1 & 0x1FF;
        if (sprite_x >= 240) sprite_x -= 512;
        if (sprite_y >= 160) sprite_y -= 256;

        // Check if sprite is on this scanline
        int local_y = y - sprite_y;
        if (local_y < 0 || local_y >= display_height) continue;

        // Get sprite attributes
        int tile_num = attr2 & 0x3FF;
        int palette = (attr2 >> 12) & 0xF;
        int prio = (attr2 >> 10) & 3;
        bool is_8bpp = attr0 & (1 << 13);
        bool hflip = !is_affine && (attr1 & (1 << 12));
        bool vflip = !is_affine && (attr1 & (1 << 13));

        // Tile base
        uint32_t tile_base = 0x10000;  // Sprite tiles start at VRAM + 0x10000

        // Mapping mode (1D vs 2D)
        bool mapping_1d = gba->ppu.dispcnt & (1 << 6);

        for (int x = 0; x < display_width; x++) {
            int screen_x = sprite_x + x;
            if (screen_x < 0 || screen_x >= 240) continue;

            int tex_x, tex_y;

            if (is_affine) {
                // TODO: Implement affine sprite transformation
                tex_x = x;
                tex_y = local_y;
                if (double_size) {
                    tex_x -= width / 2;
                    tex_y -= height / 2;
                }
            } else {
                tex_x = hflip ? (width - 1 - x) : x;
                tex_y = vflip ? (height - 1 - local_y) : local_y;
            }

            if (tex_x < 0 || tex_x >= width || tex_y < 0 || tex_y >= height) continue;

            int tile_x = tex_x / 8;
            int tile_y = tex_y / 8;
            int fine_x = tex_x & 7;
            int fine_y = tex_y & 7;

            int tile;
            if (mapping_1d) {
                if (is_8bpp) {
                    tile = tile_num + tile_y * (width / 4) + tile_x * 2;
                } else {
                    tile = tile_num + tile_y * (width / 8) + tile_x;
                }
            } else {
                if (is_8bpp) {
                    tile = (tile_num & ~1) + tile_y * 32 + tile_x * 2;
                } else {
                    tile = tile_num + tile_y * 32 + tile_x;
                }
            }

            uint8_t color_idx;
            if (is_8bpp) {
                uint32_t addr = tile_base + (tile & 0x3FF) * 32 + fine_y * 8 + fine_x;
                if (addr < VRAM_SIZE) {
                    color_idx = gba->mem.vram[addr];
                } else {
                    continue;
                }
            } else {
                uint32_t addr = tile_base + (tile & 0x3FF) * 32 + fine_y * 4 + (fine_x >> 1);
                if (addr < VRAM_SIZE) {
                    uint8_t byte = gba->mem.vram[addr];
                    color_idx = (fine_x & 1) ? (byte >> 4) : (byte & 0xF);
                } else {
                    continue;
                }
            }

            if (color_idx != 0) {
                uint32_t palette_addr;
                if (is_8bpp) {
                    palette_addr = 0x200 + color_idx * 2;
                } else {
                    palette_addr = 0x200 + (palette * 16 + color_idx) * 2;
                }

                if (priority[screen_x] > prio) {
                    line[screen_x] = *(uint16_t*)&gba->mem.palette[palette_addr];
                    priority[screen_x] = prio;
                }
            }
        }
    }
}

// Render a scanline
static void render_scanline(gba_t *gba)
{
    uint16_t *line = &gba->ppu.framebuffer[gba->ppu.vcount * GBA_SCREEN_WIDTH];
    uint8_t priority[240];

    // Fill with backdrop color
    uint16_t backdrop = *(uint16_t*)&gba->mem.palette[0];
    for (int i = 0; i < 240; i++) {
        line[i] = backdrop;
        priority[i] = 4;
    }

    // Check forced blank
    if (gba->ppu.dispcnt & (1 << 7)) {
        memset(line, 0xFF, 240 * 2);  // White screen
        return;
    }

    int mode = gba->ppu.dispcnt & 7;

    switch (mode) {
        case 0:
            // Mode 0: 4 text BGs
            for (int bg = 3; bg >= 0; bg--) {
                if (gba->ppu.dispcnt & (1 << (8 + bg))) {
                    render_bg_text(gba, bg, line);
                }
            }
            break;

        case 1:
            // Mode 1: 2 text BGs + 1 affine BG
            if (gba->ppu.dispcnt & (1 << 10)) render_bg_affine(gba, 2, line);
            if (gba->ppu.dispcnt & (1 << 9)) render_bg_text(gba, 1, line);
            if (gba->ppu.dispcnt & (1 << 8)) render_bg_text(gba, 0, line);
            break;

        case 2:
            // Mode 2: 2 affine BGs
            if (gba->ppu.dispcnt & (1 << 11)) render_bg_affine(gba, 3, line);
            if (gba->ppu.dispcnt & (1 << 10)) render_bg_affine(gba, 2, line);
            break;

        case 3:
            render_mode3(gba, line);
            break;

        case 4:
            render_mode4(gba, line);
            break;

        case 5:
            render_mode5(gba, line);
            break;
    }

    // Render sprites if enabled
    if (gba->ppu.dispcnt & (1 << 12)) {
        render_sprites(gba, line, priority);
    }
}

void ppu_step(gba_t *gba, uint32_t cycles)
{
    gba->ppu.cycle += cycles;

    while (gba->ppu.cycle >= CYCLES_PER_PIXEL) {
        gba->ppu.cycle -= CYCLES_PER_PIXEL;

        int pixel = (gba->ppu.cycle / CYCLES_PER_PIXEL) % 308;

        if (pixel == 0) {
            // Start of scanline
            if (gba->ppu.vcount < VDRAW_LINES) {
                // Render the scanline
                render_scanline(gba);
            }

            // Update affine reference points at end of each scanline
            gba->ppu.bgx[0] += gba->ppu.bgpb[0];
            gba->ppu.bgy[0] += gba->ppu.bgpd[0];
            gba->ppu.bgx[1] += gba->ppu.bgpb[1];
            gba->ppu.bgy[1] += gba->ppu.bgpd[1];
        }

        if (pixel == 240) {
            // Start of H-blank
            gba->ppu.dispstat |= 0x02;  // Set H-blank flag

            if (gba->ppu.dispstat & (1 << 4)) {
                cpu_raise_irq(gba, IRQ_HBLANK);
            }

            // Trigger H-blank DMA
            dma_trigger(gba, 2);
        }

        if (pixel == 0 && gba->ppu.vcount > 0) {
            // End of H-blank (start of next line)
            gba->ppu.dispstat &= ~0x02;
        }
    }

    // Check for scanline changes
    int new_vcount = (gba->ppu.cycle / SCANLINE_CYCLES) % TOTAL_LINES;

    if (new_vcount != gba->ppu.vcount) {
        gba->ppu.vcount = new_vcount;

        // V-count match
        uint8_t lyc = gba->ppu.dispstat >> 8;
        if (gba->ppu.vcount == lyc) {
            gba->ppu.dispstat |= 0x04;  // Set V-count flag
            if (gba->ppu.dispstat & (1 << 5)) {
                cpu_raise_irq(gba, IRQ_VCOUNT);
            }
        } else {
            gba->ppu.dispstat &= ~0x04;
        }

        // V-blank start
        if (gba->ppu.vcount == VDRAW_LINES) {
            gba->ppu.dispstat |= 0x01;  // Set V-blank flag

            if (gba->ppu.dispstat & (1 << 3)) {
                cpu_raise_irq(gba, IRQ_VBLANK);
            }

            // Trigger V-blank DMA
            dma_trigger(gba, 1);

            // Reset affine reference points
            gba->ppu.bgx[0] = gba->ppu.bgx_ref[0];
            gba->ppu.bgy[0] = gba->ppu.bgy_ref[0];
            gba->ppu.bgx[1] = gba->ppu.bgx_ref[1];
            gba->ppu.bgy[1] = gba->ppu.bgy_ref[1];

            // Frame complete callback
            if (gba->frame_callback) {
                gba->frame_callback(gba->ppu.framebuffer);
            }
        }

        // V-blank end
        if (gba->ppu.vcount == 0) {
            gba->ppu.dispstat &= ~0x01;
            gba->ppu.cycle = 0;
        }
    }
}
