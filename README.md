# libmupdf
This filter renders the first page of an XPS, CBZ, EPUB, FictionBook or Mobipocket document to a raw image using MuPDF.

> **État : non fonctionnel.** Le module se construit et se lie, mais le
> navigateur refuse de l'instancier :
> `LinkError: Import "env" "__c_longjmp": tag import requires a WebAssembly.Tag`.
>
> `fz_try`/`fz_catch` de MuPDF repose sur `setjmp`/`longjmp`, et aucune des deux
> implémentations d'emscripten ne passe avec les solveurs pré-construits :
> - SjLj JavaScript (défaut) : le module importe des trampolines `invoke_*`
>   (`invoke_dii`, `invoke_fi`, `invoke_fif`…) que le solveur n'exporte pas. Il
>   n'exporte que les signatures dont libpng et libjpeg ont besoin, lesquels
>   utilisent aussi `setjmp` mais avec bien moins de variantes.
> - SjLj WebAssembly (`-sSUPPORT_LONGJMP=wasm`, ce que fait ce CMakeLists) :
>   supprime les trampolines mais fait importer le *tag* d'exception
>   `__c_longjmp`, que le solveur ne fournit pas davantage.
>
> Il faudrait reconstruire le solveur, soit avec l'ensemble des signatures
> `invoke_*`, soit avec le même mode SjLj WebAssembly. Non testé, pas de démo.


## Requirements

[CMake](https://cmake.org/) is used as a build system. To install it, follow
[Debian build instructions](developing_in_debian.md).

[Emscripten SDK](https://emscripten.org/) is required for building
WebAssembly artifacts. To install it, follow the
[Download and Install](https://emscripten.org/docs/getting_started/downloads.html)
guide:

```bash
cd $OPT

# Get the emsdk repo.
git clone https://github.com/emscripten-core/emsdk.git

# Enter that directory.
cd emsdk

# Download and install the latest SDK tools.
./emsdk install latest

# Make the "latest" SDK "active" for the current user. (writes ~/.emscripten file)
./emsdk activate latest
```

## Building the accessor

```bash
# Setup EMSDK and other environment variables. In practice EMSDK is set to be
# $OPT/emsdk.
source $OPT/emsdk/emsdk_env.sh

# Assuming you are in the root level of the cloned repo :
emcmake cmake .
emmake make
```

Once built, you can use and distribute libmupdf_1.wasm with your universal tags.

## Documentation

For more details, please visit our documentation at https://bevara.com/documentation/develop/.
