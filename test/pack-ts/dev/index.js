
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
"NM$$ts$$_$$loader$indexDOT$$js$$B$$renderDOT$$ts":{m:function(module, exports, __fastpack_require__, __fastpack_import__) {
eval("\"use strict\";\nfunction render() {\n    document.body.innerHTML = \"<h1> Hello World from TypeScript!</h1>\";\n}\nmodule.exports = render;\n\n//# sourceURL=fpack:///node_modules/ts-loader/index.js!render.ts\n//# sourceURL=fpack:///node_modules/ts-loader/index.js!render.ts");
},
d: {}
},
"NM$$ts$$_$$loader$indexDOT$$js$$B$$indexDOT$$ts":{m:function(module, exports, __fastpack_require__, __fastpack_import__) {
eval("\"use strict\";\nexports.__esModule = true;\nvar render = __fastpack_require__(\"./render.ts\");\nrender();\n\n//# sourceURL=fpack:///node_modules/ts-loader/index.js!index.ts\n//# sourceURL=fpack:///node_modules/ts-loader/index.js!index.ts");
},
d: {"./render.ts":"NM$$ts$$_$$loader$indexDOT$$js$$B$$renderDOT$$ts"}
},
"$fp$main":{m:function(module, exports, __fastpack_require__, __fastpack_import__) {
eval("module.exports.__esModule = true;\n__fastpack_require__(\"./index.ts\");\n\n\n\n//# sourceURL=fpack:///$fp$main\n//# sourceURL=fpack:///$fp$main");
},
d: {"./index.ts":"NM$$ts$$_$$loader$indexDOT$$js$$B$$indexDOT$$ts"}
},

});
