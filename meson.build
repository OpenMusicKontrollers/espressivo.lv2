project('espressivo.lv2', 'c', default_options : [
	'buildtype=release',
	'warning_level=1',
	'werror=false',
	'b_lto=true',
	'c_std=c11'])

add_project_arguments('-D_GNU_SOURCE', language : 'c')

conf_data = configuration_data()
cc = meson.get_compiler('c')

cp = find_program('cp')
clone = [cp, '@INPUT@', '@OUTPUT@']

m_dep = cc.find_library('m')
lv2_dep = dependency('lv2', version : '>=1.14.0')

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

mod = shared_module('espressivo', dsp_srcs,
	c_args : c_args,
	include_directories : inc_dir,
	name_prefix : '',
	dependencies : [m_dep, lv2_dep],
	install : true,
	install_dir : inst_dir)

version = run_command('cat', 'VERSION').stdout().strip().split('.')
conf_data.set('MAJOR_VERSION', version[0])
conf_data.set('MINOR_VERSION', version[1])
conf_data.set('MICRO_VERSION', version[2])

suffix = mod.full_path().strip().split('.')[-1]
conf_data.set('MODULE_SUFFIX', '.' + suffix)

configure_file(input : 'manifest.ttl.in', output : 'manifest.ttl',
	configuration : conf_data,
	install : true,
	install_dir : inst_dir)
custom_target('espressivo_ttl',
	input : 'espressivo.ttl',
	output : 'espressivo.ttl',
	command : clone,
	install : true,
	install_dir : inst_dir)
