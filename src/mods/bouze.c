/*
 * Bouze (.BZZ) helpers — see bouze.h. Port of tools/bouze/bouze.py and
 * tools/bouze/records.py, which parse all 63 .BZZ files on the disc
 * exactly to their terminators.
 */
#include "bouze.h"

#include <stdlib.h>
#include <string.h>

#define BOUZE_SECTOR 2048u

int bouze_parse_header(const uint8_t* file, size_t file_len, BouzeHeader* h)
{
    if (!file || file_len < BOUZE_SECTOR || !h)
        return 0;
    memset(h, 0, sizeof(*h));
    h->version = bouze_u32(file + 0);
    h->data_off = bouze_u32(file + 4);
    h->count = bouze_u32(file + 8);
    if (h->count == 0 || h->count > BOUZE_MAX_CHUNKS)
        return 0;
    uint32_t at = BOUZE_SECTOR;
    for (uint32_t i = 0; i < h->count; ++i) {
        const uint8_t* e = file + 0x0C + 12u * i;
        h->type[i] = bouze_u32(e + 0);
        h->size[i] = bouze_u32(e + 4);
        h->aligned[i] = bouze_u32(e + 8);
        h->file_off[i] = at;
        at += h->aligned[i];
        if (h->file_off[i] + h->size[i] > file_len)
            return 0;
    }
    return 1;
}

int bouze_find_chunk(const BouzeHeader* h, uint32_t type)
{
    for (uint32_t i = 0; i < h->count; ++i)
        if (h->type[i] == type)
            return (int)i;
    return -1;
}

/* Equivalent to Bouze_Decompress: byte 0 = parameters, bytes 1..3 = token
 * count, then flag bytes (LSB first; 1 = literal, 0 = 16-bit match). The
 * game decodes one token past the count into padding; stopping at the
 * compressed size avoids that. */
uint8_t* bouze_decompress(const uint8_t* src, size_t src_len, size_t* out_len)
{
    if (!src || src_len < 4 || !out_len)
        return NULL;
    const uint8_t b0 = src[0];
    const unsigned k = b0 & 7u;
    const unsigned max_code = (0x7Fu >> k) & 0xFFFFu;
    unsigned thresh = 0x13u;
    if (max_code < 0x1Fu)
        thresh = max_code >> 1;
    const unsigned shift = (b0 >> 3) & 3u;
    uint16_t table[128];
    for (unsigned c = 0; c <= max_code && c < 128; ++c) {
        const unsigned v = c <= thresh ? c : thresh + ((c - thresh) << shift);
        table[c] = (uint16_t)((v + 2u) & 0xFFFFu);
    }
    uint32_t remaining = (uint32_t)src[3] | ((uint32_t)src[1] << 16) |
                         ((uint32_t)src[2] << 8);
    const unsigned len_mask = 0x7Fu >> k;
    const unsigned dist_shift = 7u - k;

    size_t cap = src_len * 4 + 1024, len = 0, p = 4;
    uint8_t* out = (uint8_t*)malloc(cap);
    if (!out)
        return NULL;
    for (;;) {
        if (p >= src_len)
            break;
        unsigned flags = src[p++] | 0x100u;
        for (;;) {
            const unsigned bit = flags & 1u;
            flags >>= 1;
            if (flags == 0)
                break;
            if (p >= src_len)
                goto done;
            if (len + 0x200 > cap) {
                size_t ncap = cap * 2;
                uint8_t* n = (uint8_t*)realloc(out, ncap);
                if (!n) {
                    free(out);
                    return NULL;
                }
                out = n;
                cap = ncap;
            }
            if (bit) {
                out[len++] = src[p++];
            } else {
                if (p + 1 >= src_len)
                    goto done;
                const unsigned v = ((unsigned)src[p] << 8) | src[p + 1];
                p += 2;
                const size_t dist = v >> dist_shift;
                const unsigned n = (unsigned)table[v & len_mask] + 1u;
                if (dist == 0 || dist > len) {
                    free(out);
                    return NULL;
                }
                const size_t from = len - dist;
                for (unsigned i = 0; i < n; ++i)   /* byte-wise: overlaps allowed */
                    out[len + i] = out[from + i];
                len += n;
            }
            if (remaining == 0)
                goto done;
            --remaining;
        }
    }
done:
    *out_len = len;
    return out;
}

/* Tag payload sizes per record kind, from the Level_LoadParse decompile.
 * Returns -1 when the kind is not handled by the loader. Unknown tags of a
 * handled kind have no payload (0). */
static int sub_record_size(uint8_t t)
{
    switch (t) {
    case 0x4D: return 2;  case 0x4E: return 4;  case 0x4F: return 8;
    case 0x50: return 8;  case 0x51: return 8;  case 0x52: return 8;
    case 0x53: return 6;  case 0x54: return 12; case 0x55: return 12;
    case 0x56: return 16; case 0x57: return 20; case 0x58: return 8;
    case 0x59: return 4;  case 0x5A: return 8;  case 0x5B: return 8;
    case 0x5C: return 2;  case 0x5D: return 8;  case 0x5E: return 12;
    case 0x5F: return 16; case 0x60: return 8;
    default: return -1;
    }
}

static int actor_tag_size(uint8_t t)
{
    switch (t) {
    case 0x0B: return 4;  case 0x0C: return 4;  case 0x0F: return 2;
    case 0x10: return 12; case 0x11: return 6;  case 0x12: return 12;
    case 0x13: return 1;  case 0x15: return 1;  case 0x16: return 28;
    case 0x19: return 4;  case 0x1A: return 4;  case 0x1B: return 6;
    case 0x1C: return 8;  case 0x1D: return 4;  case 0x1E: return 2;
    case 0x1F: return 1;  case 0x27: return 4;  case 0x2F: return 48;
    case 0x31: return 12; case 0x37: return 10; case 0x38: return 2;
    case 0x39: return 12; case 0x40: return 20; case 0x41: return 6;
    case 0x45: return 2;  case 0x61: return 14; case 0x65: return 2;
    default: {
        const int n = sub_record_size(t);
        return n < 0 ? 0 : n;
    }
    }
}

static int kind_handled(uint8_t kind)
{
    switch (kind) {
    case 0x00: case 0x05: case 0x07: case 0x08: case 0x09: case 0x0A:
    case 0x63: case 0x20: case 0x22: case 0x28: case 0x35: case 0x3A:
    case 0x46: case 0x48:
        return 1;
    default:
        return 0;
    }
}

static int tag_size(uint8_t kind, uint8_t t)
{
    switch (kind) {
    case 0x00:
        switch (t) {
        case 0x04: return 12; case 0x01: return 8; case 0x02: return 8;
        case 0x4B: return 56; case 0x43: return 12; case 0x4C: return 56;
        default: return 0;
        }
    case 0x05: return t == 0x06 ? 3 : 0;
    case 0x07: case 0x08: case 0x09: case 0x0A: case 0x63:
        return actor_tag_size(t);
    case 0x20:
        switch (t) {
        case 0x21: return 4; case 0x11: return 6; case 0x10: return 12;
        case 0x12: return 12; case 0x38: return 2; case 0x24: return 8;
        case 0x42: return 1;
        default: return 0;
        }
    case 0x22:
        switch (t) {
        case 0x0D: return 12; case 0x24: return 8; case 0x25: return 8;
        case 0x26: return 4;  case 0x27: return 4; case 0x3E: return 12;
        case 0x3F: return 8;
        default: {
            const int n = sub_record_size(t);
            return n < 0 ? 0 : n;
        }
        }
    case 0x28: return t == 0x29 ? 8 : 0;
    case 0x35: return t == 0x36 ? 8 : 0;
    case 0x3A:
        switch (t) {
        case 0x3D: return 36; case 0x2A: return 8; case 0x3C: return 14;
        case 0x66: return 3;
        default: return 0;
        }
    case 0x46: return t == 0x47 ? 8 : 0;
    case 0x48: return (t == 0x49 || t == 0x68) ? 56 : 0;
    default: return 0;
    }
}

int bouze_is_actor_kind(uint8_t kind)
{
    return kind == 0x07 || kind == 0x08 || kind == 0x09 || kind == 0x0A ||
           kind == 0x63;
}

int bouze_next_record(const uint8_t* s, size_t len, size_t* pos, BouzeRecord* r)
{
    size_t p = *pos;
    while (p < len) {
        const uint8_t b = s[p];
        if (b == 0x2E) {
            *pos = p;
            return 0;
        }
        if (b == 0x2D || !kind_handled(b)) {
            ++p;                       /* separator or skipped kind byte */
            continue;
        }
        r->kind = b;
        r->start = p;
        r->tags = p + 1;
        p = r->tags;
        while (p < len && s[p] != 0x2D)
            p += 1u + (size_t)tag_size(b, s[p]);
        if (p >= len)
            return -1;
        r->end = p;
        *pos = p + 1;
        return 1;
    }
    return -1;
}

int bouze_next_tag(const uint8_t* s, const BouzeRecord* r, size_t* pos, BouzeTag* t)
{
    const size_t p = *pos;
    if (p >= r->end)
        return 0;
    t->tag = s[p];
    t->payload = p + 1;
    t->len = (uint32_t)tag_size(r->kind, s[p]);
    *pos = p + 1u + t->len;
    return 1;
}
