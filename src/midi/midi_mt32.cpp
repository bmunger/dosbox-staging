/*
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *
 *  Copyright (C) 2012-2021  sergm <sergm@bigmir.net>
 *  Copyright (C) 2020-2021  Nikos Chantziaras <realnc@gmail.com> (settings)
 *  Copyright (C) 2020-2021  The DOSBox Staging Team
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

#include "midi_mt32.h"

#if C_MT32EMU

#include <cassert>
#include <deque>
#include <functional>
#include <map>
#include <set>
#include <string>

#include <SDL_endian.h>

#include "control.h"
#include "cross.h"
#include "fs_utils.h"
#include "../ints/int10.h"
#include "midi.h"
#include "midi_mt32_model.h"
#include "mixer.h"
#include "string_utils.h"
#include "support.h"

// mt32emu Settings
// ----------------

// Buffer sizes
static constexpr int FRAMES_PER_BUFFER = 1024; // synth granularity

// Analogue circuit modes: DIGITAL_ONLY, COARSE, ACCURATE, OVERSAMPLED
constexpr auto ANALOG_MODE = MT32Emu::AnalogOutputMode_ACCURATE;

// DAC Emulation modes: NICE, PURE, GENERATION1, and GENERATION2
constexpr auto DAC_MODE = MT32Emu::DACInputMode_NICE;

// Analog rendering types: BITS16S, FLOAT
constexpr auto RENDERING_TYPE = MT32Emu::RendererType_FLOAT;

// Sample rate conversion quality: FASTEST, FAST, GOOD, BEST
constexpr auto RATE_CONVERSION_QUALITY = MT32Emu::SamplerateConversionQuality_BEST;

// Use improved behavior for volume adjustments, panning, and mixing
constexpr bool USE_NICE_RAMP = true;
constexpr bool USE_NICE_PANNING = true;
constexpr bool USE_NICE_PARTIAL_MIXING = true;

// Traditional ROMs
const rom_t mt32_nover_pcm_f = {"pcm_mt32", "MT32_PCM.ROM"};
const rom_t mt32_nover_ctrl_f = {"ctrl_mt32", "MT32_CONTROL.ROM"};
const rom_t cm32l_nover_pcm_f = {"pcm_cm32l", "CM32L_PCM.ROM"};
const rom_t cm32l_nover_ctrl_f = {"ctrl_cm32l", "CM32L_CONTROL.ROM"};

// MAME ROMs
const rom_t mt32_pcm_nover_f = {"pcm_mt32", "r15449121.ic37.bin"};
const rom_t mt32_pcm_nover_l = {"pcm_mt32_l", "r15179844.ic21.bin"};
const rom_t mt32_pcm_nover_h = {"pcm_mt32_h", "r15179845.ic22.bin"};
const rom_t mt32_ctrl_1_04_a = {"ctrl_mt32_1_04_a", "mt32_1.0.4.ic27.bin"};
const rom_t mt32_ctrl_1_04_b = {"ctrl_mt32_1_04_b", "mt32_1.0.4.ic26.bin"};
const rom_t mt32_ctrl_1_05_a = {"ctrl_mt32_1_05_a", "mt32_1.0.5.ic27.bin"};
const rom_t mt32_ctrl_1_05_b = {"ctrl_mt32_1_05_b", "mt32_1.0.5.ic26.bin"};
const rom_t mt32_ctrl_1_06_a = {"ctrl_mt32_1_06_a", "mt32_1.0.6.ic27.bin"};
const rom_t mt32_ctrl_1_06_b = {"ctrl_mt32_1_06_b", "mt32_1.0.6.ic26.bin"};
const rom_t mt32_ctrl_1_07_a = {"ctrl_mt32_1_07_a", "mt32_1.0.7.ic27.bin"};
const rom_t mt32_ctrl_1_07_b = {"ctrl_mt32_1_07_b", "mt32_1.0.7.ic26.bin"};
const rom_t mt32_ctrl_bluer_a = {"ctrl_mt32_bluer_a", "blue_ridge__mt32a.bin"};
const rom_t mt32_ctrl_bluer_b = {"ctrl_mt32_bluer_b", "blue_ridge__mt32b.bin"};
const rom_t mt32_ctrl_2_04_a = {"ctrl_mt32_2_04_a", "mt32_2.0.4.ic27.bin"};
const rom_t mt32_ctrl_2_04_b = {"ctrl_mt32_2_04_b", "mt32_2.0.4.ic26.bin"};
const rom_t cm32l_pcm_nover_l = {"pcm_mt32", "r15449121.ic37.bin"};
const rom_t cm32l_pcm_nover_h = {"pcm_cm32l_h", "r15179945.ic8.bin"};
const rom_t cm32l_ctrl_1_00_f = {"ctrl_cm32l_1_00", "lapc-i.v1.0.0.ic3.bin"};
const rom_t cm32l_ctrl_1_02_f = {"ctrl_cm32l_1_02", "cm32l_control.rom"};

// Roland LA Models (composed of ROMs)
Model mt32_nover_model = {"mt32",  &mt32_nover_pcm_f,  nullptr,
                          nullptr, &mt32_nover_ctrl_f, nullptr,
                          nullptr};
Model mt32_1_04_model = {"mt32_1_04",       &mt32_pcm_nover_f,
                         &mt32_pcm_nover_l, &mt32_pcm_nover_h,
                         nullptr,           &mt32_ctrl_1_04_a,
                         &mt32_ctrl_1_04_b};
Model mt32_1_05_model = {"mt32_1_05",       &mt32_pcm_nover_f,
                         &mt32_pcm_nover_l, &mt32_pcm_nover_h,
                         nullptr,           &mt32_ctrl_1_05_a,
                         &mt32_ctrl_1_05_b};
Model mt32_1_06_model = {"mt32_1_06",       &mt32_pcm_nover_f,
                         &mt32_pcm_nover_l, &mt32_pcm_nover_h,
                         nullptr,           &mt32_ctrl_1_06_a,
                         &mt32_ctrl_1_06_b};
Model mt32_1_07_model = {"mt32_1_07",       &mt32_pcm_nover_f,
                         &mt32_pcm_nover_l, &mt32_pcm_nover_h,
                         nullptr,           &mt32_ctrl_1_07_a,
                         &mt32_ctrl_1_07_b};
Model mt32_bluer_model = {"mt32_bluer",      &mt32_pcm_nover_f,
                          &mt32_pcm_nover_l, &mt32_pcm_nover_h,
                          nullptr,           &mt32_ctrl_bluer_a,
                          &mt32_ctrl_bluer_b};
Model mt32_2_04_model = {"mt32_2_04",       &mt32_pcm_nover_f,
                         &mt32_pcm_nover_l, &mt32_pcm_nover_h,
                         nullptr,           &mt32_ctrl_2_04_a,
                         &mt32_ctrl_2_04_b};
Model cm32l_nover_model = {"cm32l", &cm32l_nover_pcm_f,  nullptr,
                           nullptr, &cm32l_nover_ctrl_f, nullptr,
                           nullptr};
Model cm32l_1_00_model = {"cm32l_1_00",
                          &cm32l_nover_pcm_f,
                          &cm32l_pcm_nover_l,
                          &cm32l_pcm_nover_h,
                          &cm32l_ctrl_1_00_f,
                          nullptr,
                          nullptr};
Model cm32l_1_02_model = {"cm32l_1_02",
                          &cm32l_nover_pcm_f,
                          &cm32l_pcm_nover_l,
                          &cm32l_pcm_nover_h,
                          &cm32l_ctrl_1_02_f,
                          nullptr,
                          nullptr};

// In order that "model = auto" will load
const std::deque<Model *> all_models = {&cm32l_nover_model, &cm32l_1_02_model,
                                        &cm32l_1_00_model,  &mt32_nover_model,
                                        &mt32_2_04_model,   &mt32_bluer_model,
                                        &mt32_1_07_model,   &mt32_1_06_model,
                                        &mt32_1_05_model,   &mt32_1_04_model};

MidiHandler_mt32 mt32_instance;

static void init_mt32_dosbox_settings(Section_prop &sec_prop)
{
	constexpr auto when_idle = Property::Changeable::WhenIdle;

	const char *models[] = {"auto",
	                        cm32l_nover_model.Name().c_str(),
	                        cm32l_1_02_model.Name().c_str(),
	                        cm32l_1_00_model.Name().c_str(),
	                        mt32_nover_model.Name().c_str(),
	                        mt32_2_04_model.Name().c_str(),
	                        mt32_bluer_model.Name().c_str(),
	                        mt32_1_07_model.Name().c_str(),
	                        mt32_1_06_model.Name().c_str(),
	                        mt32_1_05_model.Name().c_str(),
	                        mt32_1_04_model.Name().c_str(),
	                        0};
	auto *str_prop = sec_prop.Add_string("model", when_idle, "auto");
	str_prop->Set_values(models);
	str_prop->Set_help(
	        "Model of synthesizer to use. The default (auto) prefers CM-32L\n"
	        "if both sets of ROMs are provided. For early Sierra games and Dune 2\n"
	        "it's recommended to use 'mt32', while newer games typically made\n"
	        "use of the CM-32L's extra sound effects (use 'auto' or 'cm32l')");

	str_prop = sec_prop.Add_string("romdir", when_idle, "");
	str_prop->Set_help(
	        "The directory containing one or both pairs of MT-32 and/or CM-32L ROMs.\n"
	        "The files must be named in capitals, as follows:\n"
	        "  - MT-32 ROM pair: MT32_CONTROL.ROM and MT32_PCM.ROM\n"
	        "  - CM-32L ROM pair: CM32L_CONTROL.ROM and CM32L_PCM.ROM\n"
	        "The directory can be absolute or relative, or leave it blank to\n"
	        "use the 'mt32-roms' directory in your DOSBox configuration\n"
	        "directory, followed by checking other common system locations.");
}

#if defined(WIN32)

static std::deque<std::string> get_rom_dirs()
{
	return {
	        "..\\mt32-roms\\",
	        CROSS_GetPlatformConfigDir() + "mt32-roms\\",
	        "C:\\mt32-rom-data\\",
	};
}

#elif defined(MACOSX)

static std::deque<std::string> get_rom_dirs()
{
	return {
	        "../mt32-roms/",
	        CROSS_GetPlatformConfigDir() + "mt32-roms/",
	        CROSS_ResolveHome("~/Library/Audio/Sounds/MT32-Roms/"),
	        "/usr/local/share/mt32-rom-data/",
	        "/usr/share/mt32-rom-data/",
	};
}

#else

static std::deque<std::string> get_rom_dirs()
{
	// First priority is $XDG_DATA_HOME
	const char *xdg_data_home_env = getenv("XDG_DATA_HOME");
	const auto xdg_data_home = CROSS_ResolveHome(
	        xdg_data_home_env ? xdg_data_home_env : "~/.local/share");

	std::deque<std::string> dirs = {
	        "../mt32-roms/",
	        xdg_data_home + "/dosbox/mt32-roms/",
	        xdg_data_home + "/mt32-rom-data/",
	};

	// Second priority are the $XDG_DATA_DIRS
	const char *xdg_data_dirs_env = getenv("XDG_DATA_DIRS");
	if (!xdg_data_dirs_env)
		xdg_data_dirs_env = "/usr/local/share:/usr/share";

	for (auto xdg_data_dir : split(xdg_data_dirs_env, ':')) {
		trim(xdg_data_dir);
		if (xdg_data_dir.empty()) {
			continue;
		}
		const auto resolved_dir = CROSS_ResolveHome(xdg_data_dir);
		dirs.emplace_back(resolved_dir + "/mt32-rom-data/");
	}

	// Third priority is $XDG_CONF_HOME, for convenience
	dirs.emplace_back(CROSS_GetPlatformConfigDir() + "mt32-roms/");

	return dirs;
}

#endif

static std::set<Model *> has_models(const MidiHandler_mt32::service_t &service,
                                    const std::string &dir)
{
	std::set<Model *> models = {};

	for (const auto m : all_models)
		if (m->InDir(service, dir))
			models.insert(m);

	return models;
}

static const char* get_selected_model() {
	Section_prop *section = static_cast<Section_prop *>(
	        control->GetSection("mt32"));
	assert(section);
	return section->Get_string("model");
}

static std::deque<std::string> get_selected_dirs() {
	Section_prop *section = static_cast<Section_prop *>(
	        control->GetSection("mt32"));
	assert(section);

	// Get potential ROM directories from the environment
	// and/or system
	auto rom_dirs = get_rom_dirs();

	// Get the user's configured ROM directory; otherwise
	// use 'mt32-roms'
	std::string selected_romdir = section->Get_string("romdir");
	if (selected_romdir.empty()) // already trimmed
		selected_romdir = "mt32-roms";
	if (selected_romdir.back() != '/' && selected_romdir.back() != '\\')
		selected_romdir += CROSS_FILESPLIT;

	// Make sure we search the user's configured directory
	// first
	rom_dirs.emplace_front(CROSS_ResolveHome((selected_romdir)));
	return rom_dirs;
}


MIDI_RC MidiHandler_mt32::ListAll(Program *caller)
{
	service_t mt32_service = GetService();
	const std::string selected_model = get_selected_model();
	const auto rom_dirs = get_selected_dirs();

	// Get the set of models supported across the directories
	std::set<Model *> models_present;
	std::set<std::string> dirs;
	const std::string pre_space = "  ";
	const std::string column_pad = " ";
	for (const std::string &dir : get_rom_dirs()) {
		for (const auto m : has_models(mt32_service, dir)) {
			models_present.insert(m);
			dirs.insert(dir);
		}
	}
	if (models_present.empty()) {
		caller->WriteOut("  No supported models present.");
		return MIDI_RC::OK;
	}

	const size_t term_width = real_readw(BIOSMEM_SEG, BIOSMEM_NB_COLS) - 1;

	// Determine widths for the header and directories
	size_t header_width = pre_space.size();

	for (const auto m : all_models)
		header_width += m->Version().size() + 1;

	const auto remainder_for_dirs = term_width - header_width;
	const auto dirs_width = std::min(remainder_for_dirs, max_size(dirs));

	const char gray[] = "\033[30;1m";
	const char green[] = "\033[32;1m";
	const char nocolor[] = "\033[0m";

	// Print the header row of all models, graying those that aren't available
	const std::string dirs_padding(dirs_width, ' ');
	caller->WriteOut("%s%s", pre_space.c_str(), dirs_padding.c_str());
	for (const auto m : all_models) {
		if (models_present.find(m) != models_present.end()) {
			if (m->Name() == selected_model) {
				caller->WriteOut("%s%s%s%s", green,
				                 m->Version().c_str(), nocolor,
				                 column_pad.c_str());
			} else {
				caller->WriteOut("%s%s", m->Version().c_str(),
				                 column_pad.c_str());
			}
		} else {
			caller->WriteOut("%s%s%s%s", gray, m->Version().c_str(),
			                 nocolor, column_pad.c_str());
		}
	}
	caller->WriteOut("\n");

	// Print the directories and truncate as needed
	bool printed_first_match = false;
	for (const std::string &d : dirs) {
		const auto dir_models = has_models(mt32_service, d);
		std::string d_sized = d;
		if (d.size() > dirs_width) {
			const std::string cutoff = "..";
			d_sized = d.substr(0, dirs_width - cutoff.size()) + cutoff;
		} else {
			d_sized += std::string(dirs_width - d.size(), ' ');
		}
		caller->WriteOut("%s%s", pre_space.c_str(), d_sized.c_str());

		for (const auto m : all_models) {
			std::string item_check(m->Version().size() + column_pad.size(),
			                       ' ');

			if (dir_models.find(m) != dir_models.end()) {
				item_check[2] = 'x';
				if (m->Name() == selected_model && !printed_first_match) {
					caller->WriteOut("%s%s%s", green, item_check.c_str(), nocolor);
					printed_first_match = true;
					continue;
				}
			}
			caller->WriteOut("%s", item_check.c_str());
		}
		caller->WriteOut("\n");
	}
	return MIDI_RC::OK;
}

static mt32emu_report_handler_i get_report_handler_interface()
{
	class ReportHandler {
	public:
		static mt32emu_report_handler_version getReportHandlerVersionID(mt32emu_report_handler_i)
		{
			return MT32EMU_REPORT_HANDLER_VERSION_0;
		}

		static void printDebug(MAYBE_UNUSED void *instance_data,
		                       const char *fmt,
		                       va_list list)
		{
			char msg[1024];
			safe_sprintf(msg, fmt, list);
			DEBUG_LOG_MSG("MT32: %s", msg);
		}

		static void onErrorControlROM(void *)
		{
			LOG_MSG("MT32: Couldn't open Control ROM file");
		}

		static void onErrorPCMROM(void *)
		{
			LOG_MSG("MT32: Couldn't open PCM ROM file");
		}

		static void showLCDMessage(void *, const char *message)
		{
			LOG_MSG("MT32: LCD-Message: %s", message);
		}
	};

	static const mt32emu_report_handler_i_v0 REPORT_HANDLER_V0_IMPL = {
	        ReportHandler::getReportHandlerVersionID,
	        ReportHandler::printDebug,
	        ReportHandler::onErrorControlROM,
	        ReportHandler::onErrorPCMROM,
	        ReportHandler::showLCDMessage,
	        nullptr, // explicit empty function pointers
	        nullptr,
	        nullptr,
	        nullptr,
	        nullptr,
	        nullptr,
	        nullptr,
	        nullptr,
	        nullptr,
	        nullptr};

	static const mt32emu_report_handler_i REPORT_HANDLER_I = {
	        &REPORT_HANDLER_V0_IMPL};

	return REPORT_HANDLER_I;
}

MidiHandler_mt32::MidiHandler_mt32()
        : soft_limiter("MT32"),
          keep_rendering(false)
{}

MidiHandler_mt32::service_t MidiHandler_mt32::GetService()
{
	service_t mt32_service = std::make_unique<MT32Emu::Service>();
	// Has libmt32emu already created a context?
	if (!mt32_service->getContext())
		mt32_service->createContext(get_report_handler_interface(), this);
	return mt32_service;
}

static bool load_model(const MidiHandler_mt32::service_t &service,
                       const std::string &selected_model,
                       const std::deque<std::string> &rom_dirs)
{
	const bool is_auto = (selected_model == "auto");
	for (auto &model : all_models)
		if (is_auto || selected_model == model->Name())
			for (const auto &dir : rom_dirs)
				if (model->InDir(service, dir) &&
				    model->Load(service, dir))
					return true;
	return false;
}

bool MidiHandler_mt32::Open(MAYBE_UNUSED const char *conf)
{
	Close();

	service_t mt32_service = GetService();
	const std::string selected_model = get_selected_model();
	const auto rom_dirs = get_selected_dirs();

	if (!load_model(mt32_service, selected_model, rom_dirs)) {
		LOG_MSG("MT32: Cound find ROMs for model %s in directories:",
		        selected_model.c_str());
		for (const auto &dir : rom_dirs)
			LOG_MSG("MT32:  - %s", dir.c_str());
		return false;
	}

	mt32emu_rom_info rom_info;
	mt32_service->getROMInfo(&rom_info);
	LOG_MSG("MT32: Initialized %s", rom_info.control_rom_description);

	const auto mixer_callback = std::bind(&MidiHandler_mt32::MixerCallBack,
	                                      this, std::placeholders::_1);
	channel_t mixer_channel(MIXER_AddChannel(mixer_callback, 0, "MT32"),
	                        MIXER_DelChannel);

	// Let the mixer command adjust the MT32's services
	// gain-level
	const auto set_mixer_level = std::bind(&MidiHandler_mt32::SetMixerLevel,
	                                       this, std::placeholders::_1);
	mixer_channel->RegisterLevelCallBack(set_mixer_level);

	const auto sample_rate = mixer_channel->GetSampleRate();

	mt32_service->setAnalogOutputMode(ANALOG_MODE);
	mt32_service->selectRendererType(RENDERING_TYPE);
	mt32_service->setStereoOutputSampleRate(sample_rate);
	mt32_service->setSamplerateConversionQuality(RATE_CONVERSION_QUALITY);

	const auto rc = mt32_service->openSynth();
	if (rc != MT32EMU_RC_OK) {
		LOG_MSG("MT32: Error initialising emulation: %i", rc);
		return false;
	}

	mt32_service->setDACInputMode(DAC_MODE);
	mt32_service->setNiceAmpRampEnabled(USE_NICE_RAMP);
	mt32_service->setNicePanningEnabled(USE_NICE_PANNING);
	mt32_service->setNicePartialMixingEnabled(USE_NICE_PARTIAL_MIXING);

	service = std::move(mt32_service);
	channel = std::move(mixer_channel);

	// Start rendering audio
	keep_rendering = true;
	const auto render = std::bind(&MidiHandler_mt32::Render, this);
	renderer = std::thread(render);
	set_thread_name(renderer, "dosbox:mt32");
	play_buffer = playable.Dequeue(); // populate the first
	                                  // play buffer

	// Start playback
	channel->Enable(true);
	is_open = true;
	return true;
}

MidiHandler_mt32::~MidiHandler_mt32()
{
	Close();
}

// When the user runs "mixer MT32
// <percent-left>:<percent-right>", this function get those
// percents as floating point ratios (100% being 1.0f). Instead
// of post-scaling the rendered integer stream in the mixer, we
// instead provide the desired floating point scalar to the MT32
// service via its gain() interface where it can more elegantly
// adjust the level of the synthesis.

// Another nuance is that MT32's gain interface takes in a
// single float, but DOSBox's mixer accept left-and-right, so we
// apply gain using the larger of the two and then use the
// limiter's left-right ratios to scale down by lesser ratio.
void MidiHandler_mt32::SetMixerLevel(const AudioFrame &levels) noexcept
{
	const float gain = std::max(levels.left, levels.right);
	{
		const std::lock_guard<std::mutex> lock(service_mutex);
		if (service)
			service->setOutputGain(gain);
	}

	const AudioFrame desired = {levels.left / gain, levels.right / gain};
	// mt32emu generates floats between -1 and 1, so we ask
	// the soft limiter to scale these up to the INT16 range
	soft_limiter.UpdateLevels(desired, INT16_MAX);
}

void MidiHandler_mt32::Close()
{
	if (!is_open)
		return;

	// Stop playback
	if (channel)
		channel->Enable(false);

	// Stop rendering and drain the rings
	keep_rendering = false;
	if (!backstock.Size())
		backstock.Enqueue(std::move(play_buffer));
	while (playable.Size())
		play_buffer = playable.Dequeue();

	// Wait for the rendering thread to finish
	if (renderer.joinable())
		renderer.join();

	// Stop the synthesizer
	if (service) {
		service->closeSynth();
		service->freeContext();
	}

	soft_limiter.PrintStats();

	// Reset the members
	channel.reset();
	service.reset();
	soft_limiter.Reset();
	total_buffers_played = 0;
	last_played_frame = 0;

	is_open = false;
}

uint32_t MidiHandler_mt32::GetMidiEventTimestamp() const
{
	const uint32_t played_frames = total_buffers_played * FRAMES_PER_BUFFER;
	return service->convertOutputToSynthTimestamp(played_frames +
	                                              last_played_frame);
}

void MidiHandler_mt32::PlayMsg(const uint8_t *msg)
{
	const auto msg_words = reinterpret_cast<const uint32_t *>(msg);
	const std::lock_guard<std::mutex> lock(service_mutex);
	service->playMsgAt(SDL_SwapLE32(*msg_words), GetMidiEventTimestamp());
}

void MidiHandler_mt32::PlaySysex(uint8_t *sysex, size_t len)
{
	assert(len <= UINT32_MAX);
	const auto msg_len = static_cast<uint32_t>(len);
	const std::lock_guard<std::mutex> lock(service_mutex);
	service->playSysexAt(sysex, msg_len, GetMidiEventTimestamp());
}

// The callback operates at the frame-level, steadily adding
// samples to the mixer until the requested numbers of frames is
// met.
void MidiHandler_mt32::MixerCallBack(uint16_t requested_frames)
{
	while (requested_frames) {
		const auto frames_to_be_played = std::min(GetRemainingFrames(),
		                                          requested_frames);
		const auto sample_offset_in_buffer = play_buffer.data() +
		                                     last_played_frame * 2;
		channel->AddSamples_s16(frames_to_be_played, sample_offset_in_buffer);
		requested_frames -= frames_to_be_played;
		last_played_frame += frames_to_be_played;
	}
}

// Returns the number of frames left to play in the buffer.
uint16_t MidiHandler_mt32::GetRemainingFrames()
{
	// If the current buffer has some frames left, then
	// return those ...
	if (last_played_frame < FRAMES_PER_BUFFER)
		return FRAMES_PER_BUFFER - last_played_frame;

	// Otherwise put the spent buffer in backstock and get
	// the next buffer
	backstock.Enqueue(std::move(play_buffer));
	play_buffer = playable.Dequeue();
	total_buffers_played++;
	last_played_frame = 0; // reset the frame counter to the
	                       // beginning

	return FRAMES_PER_BUFFER;
}

// Keep the playable queue populated with freshly rendered buffers
void MidiHandler_mt32::Render()
{
	// Allocate our buffers once and reuse for the duration.
	constexpr auto SAMPLES_PER_BUFFER = FRAMES_PER_BUFFER * 2; // L & R
	std::vector<float> render_buffer(SAMPLES_PER_BUFFER);
	std::vector<int16_t> playable_buffer(SAMPLES_PER_BUFFER);

	// Populate the backstock using copies of the current
	// buffer.
	while (backstock.Size() < backstock.MaxCapacity() - 1)
		backstock.Enqueue(playable_buffer);
	backstock.Enqueue(std::move(playable_buffer));
	assert(backstock.Size() == backstock.MaxCapacity());

	while (keep_rendering.load()) {
		{
			const std::lock_guard<std::mutex> lock(service_mutex);
			service->renderFloat(render_buffer.data(), FRAMES_PER_BUFFER);
		}
		// Grab the next buffer from backstock and
		// populate it ...
		playable_buffer = backstock.Dequeue();
		soft_limiter.Process(render_buffer, FRAMES_PER_BUFFER,
		                     playable_buffer);

		// and then move it into the playable queue
		playable.Enqueue(std::move(playable_buffer));
	}
}

static void mt32_init(MAYBE_UNUSED Section *sec)
{}

void MT32_AddConfigSection(Config *conf)
{
	assert(conf);
	Section_prop *sec_prop = conf->AddSection_prop("mt32", &mt32_init);
	assert(sec_prop);
	init_mt32_dosbox_settings(*sec_prop);
}

#endif // C_MT32EMU
