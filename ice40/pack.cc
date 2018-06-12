/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@clifford.at>
 *  Copyright (C) 2018  David Shah <dave@ds0.me>
 *
 *  Permission to use, copy, modify, and/or distribute this software for any
 *  purpose with or without fee is hereby granted, provided that the above
 *  copyright notice and this permission notice appear in all copies.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#include "pack.h"
#include <algorithm>
#include <unordered_set>
#include "cells.h"
#include "design_utils.h"
#include "log.h"

NEXTPNR_NAMESPACE_BEGIN

// Pack LUTs and LUT-FF pairs
static void pack_lut_lutffs(Design *design)
{
    std::unordered_set<IdString> packed_cells;
    std::vector<CellInfo *> new_cells;
    for (auto cell : design->cells) {
        CellInfo *ci = cell.second;
        log_info("cell '%s' is of type '%s'\n", ci->name.c_str(),
                 ci->type.c_str());
        if (is_lut(ci)) {
            CellInfo *packed = create_ice_cell(design, "ICESTORM_LC",
                                               ci->name.str() + "_LC");
            std::copy(ci->attrs.begin(), ci->attrs.end(),
                      std::inserter(packed->attrs, packed->attrs.begin()));
            packed_cells.insert(ci->name);
            new_cells.push_back(packed);
            log_info("packed cell %s into %s\n", ci->name.c_str(),
                     packed->name.c_str());
            // See if we can pack into a DFF
            // TODO: LUT cascade
            NetInfo *o = ci->ports.at("O").net;
            CellInfo *dff = net_only_drives(o, is_ff, "D", true);
            auto lut_bel = ci->attrs.find("BEL");
            bool packed_dff = false;
            if (dff) {
                log_info("found attached dff %s\n", dff->name.c_str());
                auto dff_bel = dff->attrs.find("BEL");
                if (lut_bel != ci->attrs.end() && dff_bel != dff->attrs.end() &&
                    lut_bel->second != dff_bel->second) {
                    // Locations don't match, can't pack
                } else {
                    lut_to_lc(ci, packed, false);
                    dff_to_lc(dff, packed, false);
                    design->nets.erase(o->name);
                    if (dff_bel != dff->attrs.end())
                        packed->attrs["BEL"] = dff_bel->second;
                    packed_cells.insert(dff->name);
                    log_info("packed cell %s into %s\n", dff->name.c_str(),
                             packed->name.c_str());
                    packed_dff = true;
                }
            }
            if (!packed_dff) {
                lut_to_lc(ci, packed, true);
            }
        }
    }
    for (auto pcell : packed_cells) {
        design->cells.erase(pcell);
    }
    for (auto ncell : new_cells) {
        design->cells[ncell->name] = ncell;
    }
}

// Pack FFs not packed as LUTFFs
static void pack_nonlut_ffs(Design *design)
{
    std::unordered_set<IdString> packed_cells;
    std::vector<CellInfo *> new_cells;

    for (auto cell : design->cells) {
        CellInfo *ci = cell.second;
        if (is_ff(ci)) {
            CellInfo *packed = create_ice_cell(design, "ICESTORM_LC",
                                               ci->name.str() + "_DFFLC");
            std::copy(ci->attrs.begin(), ci->attrs.end(),
                      std::inserter(packed->attrs, packed->attrs.begin()));
            log_info("packed cell %s into %s\n", ci->name.c_str(),
                     packed->name.c_str());
            packed_cells.insert(ci->name);
            new_cells.push_back(packed);
            dff_to_lc(ci, packed, true);
        }
    }
    for (auto pcell : packed_cells) {
        design->cells.erase(pcell);
    }
    for (auto ncell : new_cells) {
        design->cells[ncell->name] = ncell;
    }
}

// Pack constants (simple implementation)
static void pack_constants(Design *design)
{
    CellInfo *gnd_cell = create_ice_cell(design, "ICESTORM_LC", "$PACKER_GND");
    gnd_cell->attrs["LUT_INIT"] = "0";

    CellInfo *vcc_cell = create_ice_cell(design, "ICESTORM_LC", "$PACKER_VCC");
    vcc_cell->attrs["LUT_INIT"] = "1";

    for (auto net : design->nets) {
        NetInfo *ni = net.second;
        if (ni->driver.cell != nullptr && ni->driver.cell->type == "GND") {
            ni->driver.cell = gnd_cell;
            ni->driver.port = "O";
            design->cells[gnd_cell->name] = gnd_cell;
        } else if (ni->driver.cell != nullptr &&
                   ni->driver.cell->type == "VCC") {
            ni->driver.cell = vcc_cell;
            ni->driver.port = "O";
            design->cells[vcc_cell->name] = vcc_cell;
        }
    }
}

// Main pack function
void pack_design(Design *design)
{
    pack_constants(design);
    pack_lut_lutffs(design);
    pack_nonlut_ffs(design);
}

NEXTPNR_NAMESPACE_END