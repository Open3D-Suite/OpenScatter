# bundle

`OpenScatter.bundle` is the package 3ds Max loads: an ApplicationPlugins bundle, the same form the
other 3ds Max tools of the Open3D suite ship in, so that the suite's launcher can install it
without opening 3ds Max.

What is in this folder is its source: `OpenScatter.bundle/PackageContents.xml`, the description
3ds Max reads. The plugin itself is a build product and is not kept here: `build.ps1 -Plugin` puts
the complete bundle together in the build folder,

```
<build>\bundle\OpenScatter.bundle\
  PackageContents.xml
  Contents\plugins\2027\OpenScatter.dlo
```

with one plugin folder per 3ds Max version (a plugin built for one version does not load in
another; only 2027 exists so far). The version is written in `CMakeLists.txt`, which also stamps it
into the file properties of the `.dlo`, and twice in `PackageContents.xml`; the build refuses to
put a bundle together when they disagree.

`tools/package.py` makes the package a release carries from it: `dist\OpenScatter.bundle`, the
same folder with `LICENSE` and `NOTICE` added, which `python -I tools/repo.py package` zips.

To use a bundle, copy the `OpenScatter.bundle` folder into an ApplicationPlugins folder
(`%APPDATA%\Autodesk\ApplicationPlugins` for one user) and start 3ds Max. To try one without
installing anything, point the environment variable `ADSK_APPLICATION_PLUGINS` at the folder that
contains it for that one 3ds Max process – which is what the tests do.

Still to come in the bundle: the scripts (menu, creation macro), the lister and the pattern files.
