module FS = FastpackUtil.FS;

type t = {
  pack:
    (
      ~current_location: option(Module.location),
      ~graph: option(DependencyGraph.t),
      ~initial: bool,
      ~start_time: float
    ) =>
    Lwt.t(result(Context.t, Context.t)),
  finalize: unit => Lwt.t(unit),
};

let make = (options: CommonOptions.t) => {
  let%lwt current_dir = Lwt_unix.getcwd();

  /* entry points */
  let%lwt entry_points =
    options.entryPoints
    |> Lwt_list.map_p(entry_point => {
         let abs_path = FS.abs_path(current_dir, entry_point);
         switch%lwt (FS.stat_option(abs_path)) {
         | Some({st_kind: Unix.S_REG, _}) =>
           Lwt.return("./" ++ FS.relative_path(current_dir, abs_path))
         | _ => Lwt.return(entry_point)
         };
       });

  let entry_location = Module.Main(entry_points);

  /* output directory & output filename */
  let (output_dir, output_file) = {
    let output_dir = FS.abs_path(current_dir, options.outputDir);
    let output_file = FS.abs_path(output_dir, options.outputFilename);
    let output_file_parent_dir = FilePath.dirname(output_file);
    if (output_dir == output_file_parent_dir
        || FilePath.is_updir(output_dir, output_file_parent_dir)) {
      (output_dir, output_file);
    } else {
      let error =
        "Output filename must be a subpath of output directory.\n"
        ++ "Output directory:\n  "
        ++ output_dir
        ++ "\n"
        ++ "Output filename:\n  "
        ++ output_file
        ++ "\n";

      raise(Context.ExitError(error));
    };
  };

  /* TODO: the next line may not belong here */
  /* TODO: also cleanup the directory before emitting, maybe? */
  let%lwt () = FS.makedirs(output_dir);

  /* project_root */
  let project_root =
    FastpackUtil.FS.abs_path(current_dir, options.projectRootDir);

  /* preprocessor */
  let%lwt preprocessor =
    Preprocessor.make(
      ~configs=options.preprocess,
      ~project_root,
      ~current_dir,
      ~output_dir,
    );

  /* cache & cache reporting */
  let%lwt (cache, cache_report) =
    switch (options.cache) {
    | Cache.Use =>
      /* TODO: consider add the --mock & --extension to the cache filename calculation */
      let filename =
        String.concat(
          "-",
          [
            current_dir |> Digest.string |> Digest.to_hex,
            Version.github_commit,
          ],
        );

      let node_modules = FilePath.concat(current_dir, "node_modules");
      let%lwt dir =
        switch%lwt (FS.try_dir(node_modules)) {
        | Some(dir) =>
          FilePath.concat(FilePath.concat(dir, ".cache"), "fpack")
          |> Lwt.return
        | None =>
          FilePath.concat(FilePath.concat(current_dir, ".cache"), "fpack")
          |> Lwt.return
        };

      let%lwt () = FS.makedirs(dir);
      let cache_filename = FilePath.concat(dir, filename);
      let%lwt cache = Cache.(create(Persistent(cache_filename)));
      Lwt.return((
        cache,
        if (cache.starts_empty) {
          "empty";
        } else {
          "used";
        },
      ));
    | Cache.Disable =>
      let%lwt cache = Cache.(create(Memory));
      Lwt.return((cache, "disabled"));
    };

  /* main package.json */
  let%lwt (project_package, _) =
    cache.find_package_for_filename(
      current_dir,
      FilePath.concat(current_dir, "package.json"),
    );

  /* make sure resolve extensions all start with '.'*/
  let extensions =
    options.resolveExtension
    |> List.filter(ext => String.trim(ext) != "")
    |> List.map(ext =>
         switch (ext.[0]) {
         | '.' => ext
         | _ => "." ++ ext
         }
       );

  let {Reporter.report_ok, report_error} = Reporter.make(options.report);
  let pack = (~current_location, ~graph, ~initial, ~start_time) => {
    let message =
      if (initial) {
        Printf.sprintf(
          " Cache: %s. Mode: %s.",
          cache_report,
          Mode.to_string(options.mode),
        );
      } else {
        "";
      };

    let resolver =
      Resolver.make(
        ~project_root,
        ~current_dir,
        ~mock=options.mock,
        ~node_modules_paths=options.nodeModulesPaths,
        ~extensions,
        ~preprocessor,
        ~cache,
      );

    let ctx = {
      Context.project_root,
      current_dir,
      project_package,
      output_dir,
      output_file,
      entry_location,
      current_location:
        CCOpt.get_or(~default=entry_location, current_location),
      stack: [],
      mode: options.mode,
      target: options.target,
      resolver,
      preprocessor,
      export_finder: ExportFinder.make(),
      graph: CCOpt.get_or(~default=DependencyGraph.empty(), graph),
      cache,
    };

    Lwt.catch(
      () => {
        let%lwt () = GraphBuilder.build(ctx);
        let%lwt (emitted_modules, files) =
          switch (options.mode) {
          | Mode.Production =>
            raise(
              Context.PackError(
                ctx,
                NotImplemented(
                  None,
                  "Production build is not implemented yet"
                  ++ "\nUse `--development` for now",
                ),
              ),
            )
          | Mode.Test
          | Mode.Development => ScopedEmitter.emit(ctx)
          };

        let ctx = {
          ...ctx,
          graph: DependencyGraph.cleanup(ctx.graph, emitted_modules),
        };
        let%lwt () =
          report_ok(~message=Some(message), ~start_time, ~ctx, ~files);
        Lwt.return_ok(ctx);
      },
      fun
      | Context.PackError(ctx, error) => {
          let%lwt () = report_error(~ctx, ~error);
          if (initial) {
            raise(Context.ExitError(""));
          } else {
            Lwt.return_error(ctx);
          };
        }
      | exn => raise(exn),
    );
  };
  let finalize = () => {
    let%lwt () = cache.dump();
    let%lwt() = preprocessor.Preprocessor.finalize();
    Lwt.return_unit;
  };
  Lwt.return({pack, finalize});
};
