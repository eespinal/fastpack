
// This function is a modified version of the one created by the Webpack project
global = window;
process = { env: {} };
(function(modules) {
  // The module cache
  var installedModules = {};

  // The require function
  function __fastpack_require__(fromModule, request) {
    var moduleId = fromModule === null ? request : modules[fromModule].d[request];

    // Check if module is in cache
    if(installedModules[moduleId]) {
      return installedModules[moduleId].exports;
    }
    // Create a new module (and put it into the cache)
    var module = installedModules[moduleId] = {
      id: moduleId,
      l: false,
      exports: {}
    };

    // Execute the module function
    modules[moduleId].m.call(
      module.exports,
      module,
      module.exports,
      __fastpack_require__.bind(null, moduleId),
      __fastpack_import__.bind(null, moduleId)
    );

    // Flag the module as loaded
    module.l = true;

    // Return the exports of the module
    return module.exports;
  }

  function __fastpack_import__(fromModule, request) {
    if (!window.Promise) {
      throw 'window.Promise is undefined, consider using a polyfill';
    }
    return new Promise(function(resolve, reject) {
      try {
        resolve(__fastpack_require__(fromModule, request));
      } catch (e) {
        reject(e);
      }
    });
  }

  __fastpack_require__.m = modules;
  __fastpack_require__.c = installedModules;
  __fastpack_require__.omitDefault = function(moduleVar) {
    var keys = Object.keys(moduleVar);
    var ret = {};
    for(var i = 0, l = keys.length; i < l; i++) {
      var key = keys[i];
      if (key !== 'default') {
        ret[key] = moduleVar[key];
      }
    }
    return ret;
  }
  return __fastpack_require__(null, __fastpack_require__.s = '$fp$main');
})
    ({
"dep":{m:function(module, exports, __fastpack_require__, __fastpack_import__) {
eval("module.exports.__esModule = true;\nexports.default = \"dep.js\";\n\n//# sourceURL=fpack:///dep.js\n//# sourceURL=fpack:///dep.js");
},
d: {}
},
"index":{m:function(module, exports, __fastpack_require__, __fastpack_import__) {
eval("var dep = __fastpack_require__(\"./dep\");\n\nfunction f(require) {\n  return require(\"./dep\");\n}\n\nfunction g() {\n  var s = f(x => x);\n  var dep2 = __fastpack_require__(\"./dep\");\n  document.body.innerHTML = `Imported: ${dep.default} Imported: ${\n    dep2.default\n  } Untouched: ${s}`;\n}\n\ng();\n\n//# sourceURL=fpack:///index.js\n//# sourceURL=fpack:///index.js");
},
d: {"./dep":"dep","./dep":"dep"}
},
"$fp$main":{m:function(module, exports, __fastpack_require__, __fastpack_import__) {
eval("module.exports.__esModule = true;\n__fastpack_require__(\"./index.js\");\n\n\n\n//# sourceURL=fpack:///$fp$main\n//# sourceURL=fpack:///$fp$main");
},
d: {"./index.js":"index"}
},

});
