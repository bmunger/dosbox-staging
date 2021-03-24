/*
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *
 *  Copyright (C) 2021-2021  The DOSBox Staging Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "midi_mt32_model.h"

#if C_MT32EMU

#include <cassert>

#include "fs_utils.h"

bool rom_t::is_versioned() const
{
	return (id.find_first_of('_') != id.find_last_of('_'));
}

Model::Model(const std::string &rom_name,
             const rom_t *pcm_full,
             const rom_t *pcm_a,
             const rom_t *pcm_b,
             const rom_t *ctrl_full,
             const rom_t *ctrl_a,
             const rom_t *ctrl_b)
        : name(rom_name),
          pcmFull(pcm_full),
          pcmA(pcm_a),
          pcmB(pcm_b),
          ctrlFull(ctrl_full),
          ctrlA(ctrl_a),
          ctrlB(ctrl_b)
{
	assert(!name.empty());
	assert(pcmFull || (pcmA && pcmB));
	assert(ctrlFull || (ctrlA && ctrlB));
}

bool Model::InDir(const service_t &service, const std::string &dir)
{
	assert(service);
	if (inDir.find(dir) != inDir.end())
		return inDir[dir];

	auto check_rom = [&](const rom_t *rom) -> bool {
		if (!rom)
			return false;

		const std::string rom_path = dir + rom->filename;
		if (!path_exists(rom_path))
			return false;

		mt32emu_rom_info info;
		const auto rcode = service->identifyROMFile(&info, rom_path.c_str(),
		                                            nullptr);
		if (rcode != MT32EMU_RC_OK)
			return false;

		if (rom->is_versioned()) {
			const bool ctrl_ver_matches = (info.control_rom_id &&
			                               rom->id == info.control_rom_id);
			const bool pcm_ver_matches = (info.pcm_rom_id &&
			                              rom->id == info.pcm_rom_id);
			return ctrl_ver_matches || pcm_ver_matches;
		}
		// Otherwise not versioned
		return true;
	};
	auto check_both = [&](const rom_t *rom_a, const rom_t *rom_b) -> bool {
		return check_rom(rom_a) && check_rom(rom_b);
	};
	const bool have_pcm = check_rom(pcmFull) || check_both(pcmA, pcmB);
	const bool have_ctrl = check_rom(ctrlFull) || check_both(ctrlA, ctrlB);
	const bool have_both = have_pcm && have_ctrl;
	inDir[dir] = have_both;
	return have_both;
}

bool Model::Load(const service_t &service, const std::string &dir)
{
	if (!service || !InDir(service, dir))
		return false;

	auto load_rom = [&service, &dir](const rom_t *rom,
	                                 mt32emu_return_code expected_code) -> bool {
		if (!rom)
			return false;
		const std::string rom_path = dir + rom->filename;
		const auto rcode = service->addROMFile(rom_path.c_str());
		return rcode == expected_code;
	};
	auto load_pair = [&service, &dir](const rom_t *rom_a, const rom_t *rom_b,
	                                  mt32emu_return_code expected_code) -> bool {
		if (!rom_a || !rom_b)
			return false;
		const std::string rom_a_path = dir + rom_a->filename;
		const std::string rom_b_path = dir + rom_b->filename;
		const auto rcode = service->mergeAndAddROMFiles(rom_a_path.c_str(),
		                                                rom_b_path.c_str());
		return rcode == expected_code;
	};
	const bool loaded_pcm = load_rom(pcmFull, MT32EMU_RC_ADDED_PCM_ROM) ||
	                        load_pair(pcmA, pcmB, MT32EMU_RC_ADDED_PCM_ROM);
	const bool loaded_ctrl = load_rom(ctrlFull, MT32EMU_RC_ADDED_CONTROL_ROM) ||
	                         load_pair(ctrlA, ctrlB, MT32EMU_RC_ADDED_CONTROL_ROM);
	return loaded_pcm && loaded_ctrl;
}

const std::string &Model::Version()
{
	if (!version.empty())
		return version;

	const auto pos = name.find_first_of('_');
	version = (pos == std::string::npos) ? name : name.substr(pos + 1);
	return version;
}
const std::string &Model::Name() const
{
	return name;
}

bool Model::operator<(const Model &other) const
{
	return name < other.Name();
}

bool Model::operator==(const Model &other) const
{
	return name == other.Name();
}

#endif // C_MT32EMU
