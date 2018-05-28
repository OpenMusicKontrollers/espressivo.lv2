project('espressivo.lv2', 'c', default_options : [
	'buildtype=release',
	'warning_level=1',
	'werror=true',
	'b_lto=true',
	'c_std=c11'])

add_project_arguments('-D_GNU_SOURCE', language : 'c')

conf_data = configuration_data()
cc = meson.get_compiler('c')

cp = find_program('cp')
lv2_validate = find_program('lv2_validate', native : true, required : false)
lv2lint = find_program('lv2lint', required : false)
clone = [cp, '@INPUT@', '@OUTPUT@']

m_dep = cc.find_library('m')
lv2_dep = dependency('lv2', version : '>=1.14.0')
deps = [m_dep, lv2_dep]

props_inc = include_directories('props.lv2')
osc_inc = include_directories('osc.lv2')
canvas_inc = include_directories('canvas.lv2')
xpress_inc = include_directories('xpress.lv2')
inc_dir = [props_inc, osc_inc, canvas_inc, xpress_inc]

inst_dir = join_paths(get_option('libdir'), 'lv2', meson.project_name())

dsp_srcs = ['espressivo.c',
	'chord_flt.c',
	'discreto_flt.c',
	'midi_in.c',
	'midi_out.c',
	'modulator_flt.c',
	'monitor_out.c',
	'mpe_in.c',
	'mpe_out.c',
	'redirector_flt.c',
	'reducto_flt.c',
	'sampleandhold_flt.c',
	'sc_out.c',
	'sqew_flt.c',
	'through_flt.c',
	'tuio2_in.c',
	'tuio2_out.c']

c_args = ['-fvisibility=hidden',
	'-ffast-math']

if host_machine.system() == 'windows'
	# nothing
elif host_machine.system() == 'darwin'
	# nothing
else
	rt_dep = cc.find_library('rt')
	deps += rt_dep
endif

mod = shared_module('espressivo', dsp_srcs,
	c_args : c_args,
	include_directories : inc_dir,
	name_prefix : '',
	dependencies : deps,
	install : true,
	install_dir : inst_dir)

version = run_command('cat', 'VERSION').stdout().strip().split('.')
conf_data.set('MAJOR_VERSION', version[0])
conf_data.set('MINOR_VERSION', version[1])
conf_data.set('MICRO_VERSION', version[2])

suffix = mod.full_path().strip().split('.')[-1]
conf_data.set('MODULE_SUFFIX', '.' + suffix)

manifest_ttl = configure_file(input : 'manifest.ttl.in', output : 'manifest.ttl',
	configuration : conf_data,
	install : true,
	install_dir : inst_dir)
dsp_ttl = custom_target('espressivo_ttl',
	input : 'espressivo.ttl',
	output : 'espressivo.ttl',
	command : clone,
	install : true,
	install_dir : inst_dir)

if lv2_validate.found()
	test('LV2 validate', lv2_validate,
		args : [manifest_ttl, dsp_ttl])
endif

if lv2lint.found()
	test('LV2 lint', lv2lint,
		args : ['-Ewarn',
			'http://open-music-kontrollers.ch/lv2/espressivo#chord',
			'http://open-music-kontrollers.ch/lv2/espressivo#discreto',
			'http://open-music-kontrollers.ch/lv2/espressivo#midi_in',
			'http://open-music-kontrollers.ch/lv2/espressivo#modulator',
			'http://open-music-kontrollers.ch/lv2/espressivo#monitor_out',
			'http://open-music-kontrollers.ch/lv2/espressivo#mpe_in',
			'http://open-music-kontrollers.ch/lv2/espressivo#mpe_out',
			'http://open-music-kontrollers.ch/lv2/espressivo#redirector',
			'http://open-music-kontrollers.ch/lv2/espressivo#reducto',
			'http://open-music-kontrollers.ch/lv2/espressivo#sc_out',
			'http://open-music-kontrollers.ch/lv2/espressivo#snh',
			'http://open-music-kontrollers.ch/lv2/espressivo#sqew',
			'http://open-music-kontrollers.ch/lv2/espressivo#through',
			'http://open-music-kontrollers.ch/lv2/espressivo#tuio2_in',
			'http://open-music-kontrollers.ch/lv2/espressivo#tuio2_out'])
endif
