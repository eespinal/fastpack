module MLSet = Module.LocationSet;
module StringSet = Set.Make(String);
module M = Map.Make(String);

module Ast = FlowParser.Ast;
module Loc = FlowParser.Loc;
module S = Ast.Statement;
module E = Ast.Expression;
module P = Ast.Pattern;
module L = Ast.Literal;

module UTF8 = FastpackUtil.UTF8;
module FS = FastpackUtil.FS;
module Parser = FastpackUtil.Parser;
module Scope = FastpackUtil.Scope;
module Visit = FastpackUtil.Visit;

let debug = Logs.debug;
let re_name = Re_posix.compile_pat("[^A-Za-z0-9_]+");

type dependencies =
  | NoDependendencies
  | Dependencies(list(Module.Dependency.t), list(Module.Dependency.t));

let resolve = (ctx: Context.t, request: Module.Dependency.t) => {
  let basedir =
    switch (request.requested_from) {
    | Module.File({filename: Some(filename), _}) =>
      FilePath.dirname(filename)
    | Module.File({filename: None, _})
    | Module.Runtime
    | Module.EmptyModule
    | Module.Main(_) => ctx.current_dir
    };

  Lwt.catch(
    () => ctx.resolver.resolve(~basedir, request.request),
    fun
    | Resolver.Error(path) =>
      Lwt.fail(Context.PackError(ctx, CannotResolveModule(path, request)))
    | exn => raise(exn),
  );
};

let is_json = (location: Module.location) =>
  switch (location) {
  | Module.File({filename: Some(filename), _}) =>
    String.suffix(~suf=".json", filename)
  | _ => false
  };

let get_module_type = stmts => {
  /* TODO: what if module has only import() expression? */
  let import_or_export = (module_type, (_, stmt): S.t(Loc.t)) =>
    switch (module_type) {
    | Module.ESM
    | Module.CJS_esModule => module_type
    | Module.CJS =>
      switch (stmt) {
      | S.Expression({
          expression: (
            _,
            E.Assignment({
              operator: E.Assignment.Assign,
              left: (
                _,
                P.Expression((
                  _,
                  E.Member({
                    _object: (_, E.Identifier((_, "exports"))),
                    property: E.Member.PropertyIdentifier((_, "__esModule")),
                    computed: false,
                    _,
                  }),
                )),
              ),
              right: (_, E.Literal({value: L.Boolean(true), _})),
            }),
          ),
          _,
        }) => Module.CJS_esModule
      | S.ExportDefaultDeclaration(_)
      | S.ExportNamedDeclaration(_)
      | S.ImportDeclaration(_) => Module.ESM
      | _ => module_type
      }
    };

  List.fold_left(import_or_export, Module.CJS, stmts);
};

let read_module =
    (~from_module=None, ~ctx: Context.t, location: Module.location) => {
  /* Logs.debug(x => x("READING: %s", Module.location_to_string(location))); */
  let make_module = (location, source) => {
    let%lwt package =
      switch (location) {
      | Module.EmptyModule
      | Module.Runtime => Lwt.return(Package.empty)
      | Module.Main(_) => Lwt.return(ctx.project_package)
      | Module.File({filename: Some(filename), _}) =>
        let%lwt (package, _) =
          ctx.cache.find_package_for_filename(ctx.current_dir, filename);

        Lwt.return(package);
      | Module.File({filename: None, _}) =>
        switch (from_module) {
        | None => Lwt.return(ctx.project_package)
        | Some((m: Module.t)) => Lwt.return(m.package)
        }
      };

    let m =
      Module.{
        id: make_id(ctx.project_root, location),
        location,
        package,
        static_dependencies: [],
        dynamic_dependencies: [],
        build_dependencies: M.empty,
        module_type: Module.CJS,
        files: [],
        source,
        scope: FastpackUtil.Scope.empty,
        exports: FastpackUtil.Scope.empty_exports,
      };
    Lwt.return((m, NoDependendencies));
  };

  let process_source = () => {
    let%lwt source =
      switch (location) {
      | Module.Main(entry_points) =>
        entry_points
        |> List.map(req => Printf.sprintf("import '%s';\n", req))
        |> String.concat("")
        |> Lwt.return_some

      | Module.EmptyModule => Lwt.return_some("module.exports = {};")

      | Module.Runtime => Lwt.return_some(FastpackTranspiler.runtime)

      | Module.File({filename, _}) =>
        switch (filename) {
        | Some(filename) =>
          let%lwt _ =
            if (!FilePath.is_subdir(filename, ctx.project_root)) {
              Lwt.fail(
                Context.PackError(ctx, CannotLeavePackageDir(filename)),
              );
            } else {
              Lwt.return_unit;
            };

          let%lwt ({content, _}, _) =
            Lwt.catch(
              () => ctx.cache.get_file(filename),
              fun
              | Cache.FileDoesNotExist(filename) =>
                Lwt.fail(Context.PackError(ctx, CannotReadModule(filename)))
              | exn => raise(exn),
            );

          /* strip #! from the very beginning */
          let content_length = String.length(content);
          let content =
            if (content_length > 2) {
              if (content.[0] == '#' && content.[1] == '!') {
                let nl_index = String.find(~sub="\n", content);
                String.sub(content, nl_index, content_length - nl_index);
              } else {
                content;
              };
            } else {
              content;
            };

          Lwt.return_some(content);
        | None => Lwt.return_none
        }
      };

    let self_dependency =
      switch (location) {
      | File({filename: Some(filename), _}) => [filename]
      | _ => []
      };

    /* Make sure module depends on the source file */
    DependencyGraph.add_build_dependencies(
      ctx.graph,
      self_dependency,
      location,
    );

    switch (is_json(location), source) {
    | (true, Some(source)) =>
      make_module(location, "module.exports = " ++ source ++ ";")
    | (true, None) => failwith("impossible: *.json file without source")
    | (false, _) =>
      let {Worker.Reader.read, _} = ctx.reader;
      let%lwt {
        Worker.source,
        static_dependencies,
        dynamic_dependencies,
        module_type,
        scope,
        exports,
        build_dependencies,
        files,
      } =
        switch%lwt (read(~location, ~source)) {
        | Complete(data) =>
          /* Logs.debug(x => x("success!")); */
          Lwt.return(data)
        | ParseError(args) =>
          let location_str = Module.location_to_string(location);
          let src =
            switch (source) {
            | Some(src) => src
            | None => ""
            };
          Lwt.fail(
            Context.PackError(
              ctx,
              CannotParseFile((location_str, args, src)),
            ),
          );
        | ScopeError(reason) =>
          Lwt.fail(Context.PackError(ctx, ScopeError(reason)))
        | PreprocessorError(message) =>
          Lwt.fail(Context.PackError(ctx, PreprocessorError(message)))
        | UnhandledCondition(message)
        | Traceback(message) =>
          Lwt.fail(Context.PackError(ctx, UnhandledCondition(message)))
        };

      /* module also depends on the filenames used to transpile it*/
      DependencyGraph.add_build_dependencies(
        ctx.graph,
        build_dependencies,
        location,
      );

      let%lwt files =
        Lwt_list.map_s(
          filename => {
            let%lwt ({content, _}, _) = ctx.cache.get_file(filename);
            Lwt.return((filename, content));
          },
          files,
        );

      let%lwt (m, _) = make_module(location, source);
      let%lwt build_dependencies =
        Lwt_list.fold_left_s(
          (build_dependencies, filename) => {
            let%lwt ({digest, _}, _) = ctx.cache.get_file(filename);
            Lwt.return(M.add(filename, digest, build_dependencies));
          },
          M.empty,
          self_dependency @ build_dependencies,
        );

      Lwt.return((
        {...m, module_type, scope, exports, build_dependencies, files},
        Dependencies(static_dependencies, dynamic_dependencies),
      ));
    };
  };

  switch (location) {
  | Module.File(_) =>
    switch%lwt (ctx.cache.get_module(location)) {
    | Some(m) => Lwt.return((m, NoDependendencies))
    | None => process_source()
    }
  | _ => process_source()
  };
};

let build = (ctx: Context.t) => {
  /* TODO: handle this at a higher level, IllegalConfiguration error */
  let%lwt () =
    if (ctx.Context.target == Target.ESM) {
      Lwt.fail(
        Context.PackError(
          ctx,
          NotImplemented(
            None,
            "EcmaScript6 target is not supported "
            ++ "for the regular packer - use flat\n",
          ),
        ),
      );
    } else {
      Lwt.return_unit;
    };

  /* Gather dependencies */
  let rec process = (ctx: Context.t, location: Module.location) =>
    switch (DependencyGraph.lookup_module(ctx.graph, location)) {
    | Some(mPromise) => mPromise
    | None =>
      DependencyGraph.add_module(
        ctx.graph,
        location,
        {
          let ctx = {...ctx, current_location: location};
          let%lwt (m, deps) = read_module(~ctx, ~from_module=None, location);
          let graph = ctx.graph;
          let%lwt m =
            switch (deps) {
            | NoDependendencies => Lwt.return(m)
            | Dependencies(static_dependencies, dynamic_dependencies) =>
              let resolve_dependencies =
                Lwt_list.map_p(req => {
                  let%lwt (resolved, build_dependencies) = resolve(ctx, req);

                  Lwt.return(((req, resolved), build_dependencies));
                });

              let%lwt static_dependencies =
                resolve_dependencies(static_dependencies);
              let%lwt dynamic_dependencies =
                resolve_dependencies(dynamic_dependencies);

              let collect_dependencies = (dependencies, build_dependencies) =>
                Lwt_list.fold_left_s(
                  ((resolved, build), (r, b)) => {
                    let%lwt build =
                      Lwt_list.fold_left_s(
                        (build, filename) => {
                          let%lwt ({digest, _}, _) =
                            ctx.cache.get_file(filename);
                          Lwt.return(M.add(filename, digest, build));
                        },
                        build,
                        b,
                      );

                    Lwt.return(([r, ...resolved], build));
                  },
                  ([], build_dependencies),
                  dependencies,
                );

              let build_dependencies = m.build_dependencies;
              let%lwt (static_dependencies, build_dependencies) =
                collect_dependencies(static_dependencies, build_dependencies);

              let%lwt (dynamic_dependencies, build_dependencies) =
                collect_dependencies(
                  dynamic_dependencies,
                  build_dependencies,
                );

              Lwt.return({
                ...m,
                static_dependencies: List.rev(static_dependencies),
                dynamic_dependencies: List.rev(dynamic_dependencies),
                build_dependencies,
              });
            };

          /* TODO: re-evaluate if we want to modify cache at this point
                   maybe it should go after module is successfully emitted
           */
          ctx.cache.modify_content(m);

          let updateGraph = (~kind, dependencies) => {
            let%lwt () =
              Lwt_list.iter_p(
                ((req, resolved)) => {
                  let%lwt () =
                    switch (
                      DependencyGraph.lookup_module(ctx.graph, resolved)
                    ) {
                    | None =>
                      let%lwt _ =
                        process(
                          {...ctx, stack: [req, ...ctx.stack]},
                          resolved,
                        );
                      Lwt.return_unit;
                    | Some(_) => Lwt.return_unit
                    };
                  Lwt.return_unit;
                },
                dependencies,
              );

            List.iter(
              ((req, resolved)) =>
                DependencyGraph.add_dependency(
                  ~kind,
                  graph,
                  m,
                  (req, resolved),
                ),
              dependencies,
            );
            Lwt.return_unit;
          };

          let%lwt () = updateGraph(~kind=`Static, m.static_dependencies);
          let%lwt () = updateGraph(~kind=`Dynamic, m.dynamic_dependencies);
          Lwt.return(m);
        },
      )
    };

  let {Context.current_location, _} = ctx;
  let%lwt _ = process(ctx, current_location);
  Lwt.return_unit;
};
