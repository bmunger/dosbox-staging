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

#ifndef DOSBOX_MIDI_MT32_MODEL_H
#define DOSBOX_MIDI_MT32_MODEL_H

#include "dosbox.h"

#if C_MT32EMU

#include <map>
#include <memory>
#include <string>

#define MT32EMU_API_TYPE 3
#include <mt32emu/mt32emu.h>

struct rom_t {
	const std::string id;
	const std::string filename;
	bool is_versioned() const;
};

// A Model consists of a PCM and Control ROM either in full or partial form
class Model {
public:
	using service_t = std::unique_ptr<MT32Emu::Service>;

	Model(const std::string &rom_name,
	      const rom_t *pcm_full,
	      const rom_t *pcm_a,
	      const rom_t *pcm_b,
	      const rom_t *ctrl_full,
	      const rom_t *ctrl_a,
	      const rom_t *ctrl_b);

	bool operator<(const Model &other) const;
	bool operator==(const Model &other) const;

	bool InDir(const service_t &service, const std::string &dir);
	bool Load(const service_t &service, const std::string &dir);
	const std::string &Name() const;
	const std::string &Version();

private:
	Model() = delete;
	Model(Model &) = delete;
	Model &operator=(Model &) = delete;

	std::map<std::string, bool> inDir = {};
	const std::string name = {};
	std::string version = {};
	const rom_t *pcmFull = nullptr;
	const rom_t *pcmA = nullptr;
	const rom_t *pcmB = nullptr;
	const rom_t *ctrlFull = nullptr;
	const rom_t *ctrlA = nullptr;
	const rom_t *ctrlB = nullptr;
};

#endif // C_MT32EMU

#endif
