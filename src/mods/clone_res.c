/*
 * Clone resources — see clone_res.h.
 */
#include "clone_res.h"

#include <string.h>

#include "bouze.h"

static void put_u32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

int clone_build_resources(const CloneResIn* in, int n, const Donor* d,
                          const CloneResLayout* lay, const DonorAnimMap* map,
                          int map_count, const uint16_t* defaults, int default_count,
                          uint8_t* nodes_out, uint8_t* entries_out, uint8_t* records_out,
                          CloneResStats* st)
{
    memset(st, 0, sizeof(*st));
    if (n <= 0 || n > CLONE_RES_MAX || !d->image || d->model_count == 0 || d->anim_count == 0)
        return -1;
    int records = 0;
    for (int i = 0; i < n; ++i) {
        uint8_t* node = nodes_out + RES_NODE_SIZE * (uint32_t)i;
        uint8_t* e = entries_out + RES_ENTRY_SIZE * (uint32_t)i;
        memset(node, 0, RES_NODE_SIZE);
        put_u32(node, i + 1 < n ? lay->nodes + RES_NODE_SIZE * (uint32_t)(i + 1) : 0u);
        put_u32(node + 8, lay->entries + RES_ENTRY_SIZE * (uint32_t)i);

        memcpy(e, in[i].entry, RES_ENTRY_SIZE);
        put_u32(e, 0);
        const uint16_t type = bouze_u16(e + 0x08);
        const uint16_t sub = bouze_u16(e + 0x0A);
        if (type == RES_TYPE_MODEL) {
            int m = 0;
            for (int k = 0; k < d->model_count; ++k)
                if (d->models[k].sub == sub) {
                    m = k;
                    break;
                }
            put_u32(e + 0x18, lay->model_data[m]);
            ++st->models;
        } else if (type == RES_TYPE_ANIM) {
            const uint8_t* src = in[i].record;
            const uint16_t own_frames = bouze_u16(src + 0x08);
            int mapped = 0;
            const DonorBlob* b = donor_pick_anim(d, sub, own_frames, map, map_count,
                                                 defaults, default_count, &mapped);
            const uint8_t* blob = d->image + b->off;
            uint8_t* r = records_out + ANIM_RECORD_SIZE * (uint32_t)records;
            memcpy(r, src, 8);
            const uint16_t frames = donor_anim_frames(b, own_frames);
            r[0x08] = (uint8_t)frames;
            r[0x09] = (uint8_t)(frames >> 8);
            r[0x0A] = blob[12];
            r[0x0B] = src[0x0B];
            put_u32(r + 0x0C, lay->image + b->off + 8u);
            put_u32(e + 0x18, lay->records + ANIM_RECORD_SIZE * (uint32_t)records);
            ++records;
            if (mapped)
                ++st->anims_mapped;
            else
                ++st->anims_default;
        } else {
            ++st->other;
        }
    }
    return records;
}
