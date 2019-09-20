mans = [
  'nm-applet.1',
  'nm-connection-editor.1'
]

man1_dir = join_paths(nma_mandir, 'man1')

man_subst = configuration_data()
man_subst.set('VERSION', nma_version)

foreach man: mans
  configure_file(
    input: man + '.in',
    output: man,
    install_dir: man1_dir,
    configuration: man_subst,
  )
endforeach
