#!/usr/bin/env python3
"""Render DX7 single-voice .syx files through Dexed (VST3), for compare.py.

  dexed_render.py syx_dir out_dir      (needs: pip install pedalboard mido numpy)

Plays <out_dir>/../schedule.mid (compare_midi.py) with each voice of syx_dir
("NN name.syx", 163-byte single-voice dumps) and writes out_dir/NN_name.wav.
The voice is set through Dexed's 155 plugin parameters (SysEx sent through
pedalboard is not applied). Dexed path: /usr/lib/vst3/Dexed.vst3.
"""
import sys, glob, os, numpy as np, mido, wave
from pedalboard import load_plugin
SR = 44100
src, out = sys.argv[1], sys.argv[2]
mid = mido.MidiFile(os.path.join(os.path.dirname(out.rstrip('/')), 'schedule.mid'))
notes, t = [], 0.0
for m in mid:
    t += m.time
    if m.type in ('note_on', 'note_off'):
        notes.append(m.copy(time=t))
end = t
OPF = [('eg_rate_1',99),('eg_rate_2',99),('eg_rate_3',99),('eg_rate_4',99),('eg_level_1',99),('eg_level_2',99),
       ('eg_level_3',99),('eg_level_4',99),('break_point',99),('l_scale_depth',99),('r_scale_depth',99),
       ('l_key_scale',3),('r_key_scale',3),('rate_scaling',7),('a_mod_sens',3),('key_velocity',7),
       ('output_level',99),('mode',1),('f_coarse',31),('f_fine',99),('osc_detune',14)]
GF = [('pitch_eg_rate_1',99),('pitch_eg_rate_2',99),('pitch_eg_rate_3',99),('pitch_eg_rate_4',99),
      ('pitch_eg_level_1',99),('pitch_eg_level_2',99),('pitch_eg_level_3',99),('pitch_eg_level_4',99),
      ('algorithm',31),('feedback',7),('osc_key_sync',1),('lfo_speed',99),('lfo_delay',99),('lfo_pm_depth',99),
      ('lfo_am_depth',99),('lfo_key_sync',1),('lfo_wave',5),('p_mode_sens',7),('transpose',48)]
p = load_plugin('/usr/lib/vst3/Dexed.vst3')
for f in sorted(glob.glob(os.path.join(src, '[0-9]*.syx'))):
    v = open(f, 'rb').read()[6:161]
    bad = 0
    for opi in range(6):
        for j, (name, mx) in enumerate(OPF):
            k = f'op{6 - opi}_{name}'
            p.parameters[k].raw_value = v[opi * 21 + j] / mx
    for j, (k, mx) in enumerate(GF):
        p.parameters[k].raw_value = v[126 + j] / mx
    for opi in range(6):
        for j, (name, mx) in enumerate(OPF):
            if round(p.parameters[f'op{6 - opi}_{name}'].raw_value * mx) != v[opi * 21 + j]: bad += 1
    for j, (k, mx) in enumerate(GF):
        if round(p.parameters[k].raw_value * mx) != v[126 + j]: bad += 1
    y = p(notes, duration=end, sample_rate=SR, num_channels=2, reset=True)
    x = y.mean(axis=0)
    name = os.path.basename(f)[:-4].replace(' ', '_')
    w = wave.open(os.path.join(out, name + '.wav'), 'wb')
    w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
    w.writeframes((np.clip(x, -1, 1) * 32767).astype(np.int16).tobytes())
    w.close()
    print(f'{name:18s} peak {np.max(np.abs(x)):.2f}  params not taken: {bad}')
