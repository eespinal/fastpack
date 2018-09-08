open Cmdliner;

let exits = Term.default_exits;
let sdocs = Manpage.s_common_options;
let version =
  Version.(Printf.sprintf("%s (Commit: %s)", version, github_commit));

let run = (debug, f) => {
  if (debug) {
    Logs.set_level(Some(Logs.Debug));
    Logs.set_reporter(Logs_fmt.reporter());
  };
  try (`Ok(f())) {
  | Context.ExitError(message) =>
    Lwt_main.run(Lwt_io.(write(stderr, message)));
    /* supress the default behaviour of the cmdliner, since it does a lot
     * of smart stuff */
    Format.(
      pp_set_formatter_out_functions(
        err_formatter,
        {
          out_string: (_, _, _) => (),
          out_flush: () => (),
          out_newline: () => (),
          out_spaces: _ => (),
          out_indent: _ => (),
        },
      )
    );
    `Error((false, message));
  | Context.ExitOK => `Ok()
  };
};

module Build = {
  let run = (options: CommonOptions.t) =>
    run(options.debug, () =>
      Lwt_main.run(
        {
          let start_time = Unix.gettimeofday();
          let%lwt {Packer.pack, finalize} = Packer.make(options);

          Lwt.finalize(
            () =>
              switch%lwt (
                pack(
                  ~graph=None,
                  ~current_location=None,
                  ~initial=true,
                  ~start_time,
                )
              ) {
              | Error(_) => raise(Context.ExitError(""))
              | Ok(_) => Lwt.return_unit
              },
            finalize,
          );
        },
      )
    );
  let doc = "rebuild the bundle on a file change";
  let command = (
    Term.(ret(const(run) $ CommonOptions.term)),
    Term.info("build", ~doc, ~sdocs, ~exits),
  );
};

module Watch = {
  let run = (options: CommonOptions.t) =>
    run(options.debug, () =>
      Lwt_main.run(
        {
          let start_time = Unix.gettimeofday();
          /* TODO: maybe decouple mode from the CommonOptions ? */
          let%lwt {Packer.pack, finalize} =
            Packer.make({...options, mode: Mode.Development});

          Lwt.finalize(
            () =>
              switch%lwt (
                pack(
                  ~graph=None,
                  ~current_location=None,
                  ~initial=true,
                  ~start_time,
                )
              ) {
              | Error(_) => raise(Context.ExitError(""))
              | Ok(ctx) => Watcher.watch(~ctx, ~pack)
              },
            finalize,
          );
        },
      )
    );
  let doc = "build the bundle";
  let command = (
    Term.(ret(const(run) $ CommonOptions.term)),
    Term.info("watch", ~doc, ~sdocs, ~exits),
  );
};

module Serve = {
  let run = (options: CommonOptions.t) =>
    run(options.debug, () =>
      Lwt_main.run(
        {
          let start_time = Unix.gettimeofday();

          let (broadcastToWebsocket, devserver) =
            FastpackServer.Devserver.start(
              ~port=3000,
              ~outputDir=options.outputDir,
              ~debug=options.debug,
              (),
            );

          let%lwt () =
            FastpackServer.CopyPublic.copy(
              ~sourceDir="./public",
              ~outputDir=options.outputDir,
              ~outputFilename=options.outputFilename,
              ~port=3000,
              (),
            );

          let report_ok =
              (
                ~message as _message,
                ~start_time as _start_time,
                ~ctx as _ctx,
                ~files as _file,
              ) => {
            print_endline("built successfully!");

            Yojson.Basic.(
              `Assoc([("build", `String("OK"))])
              |> to_string(~std=true)
              |> (s => s ++ "\n")
              |> broadcastToWebsocket
            );
          };

          let report_error = (~ctx, ~error) => {
            print_endline("error occured!");

            Yojson.Basic.(
              `Assoc([
                ("error", `String(Context.stringOfError(ctx, error))),
              ])
              |> to_string(~std=true)
              |> (s => s ++ "\n")
              |> broadcastToWebsocket
            );
          };

          /* TODO: maybe decouple mode from the CommonOptions ? */
          let%lwt {Packer.pack, finalize} =
            Packer.make({
              ...options,
              mode: Mode.Development,
              report: Reporter.Internal(report_ok, report_error),
            });

          Lwt.finalize(
            () =>
              switch%lwt (
                pack(
                  ~graph=None,
                  ~current_location=None,
                  ~initial=true,
                  ~start_time,
                )
              ) {
              | Error(_) => raise(Context.ExitError(""))
              | Ok(ctx) =>
                /* super-functional web-server :) */
                let server = devserver;
                let watcher = Watcher.watch(~ctx, ~pack);
                Lwt.join([server, watcher]);
              },
            finalize,
          );
        },
      )
    );
  let doc = "watch for file changes, rebuild bundle & serve";
  let command = (
    /* TODO: add options here */
    Term.(ret(const(run) $ CommonOptions.term)),
    Term.info("serve", ~doc, ~sdocs, ~exits),
  );
};

module ParsingServer = {
  let run = (options: CommonOptions.t) => {
    let {CommonOptions.projectRootDir: project_root, outputDir: output_dir, _} = options;
    let current_dir = Unix.getcwd();
    Lwt_main.run(
      ParsingServer.start(~project_root, ~current_dir, ~output_dir),
    );
  };
  let doc = "parsing service";
  let command = (
    Term.(ret(const(run) $ CommonOptions.term)),
    Term.info("parsing-server", ~doc, ~sdocs, ~exits),
  );
};

module Help = {
  let run = () => `Help((`Auto, None));
  let command = (
    Term.(ret(const(run) $ const())),
    Term.info(
      "help",
      ~version,
      ~doc="Show this message and exit",
      ~sdocs,
      ~exits,
    ),
  );
};

module Default = {
  let command = (
    fst(Build.command),
    Term.info(
      "fpack",
      ~version,
      ~doc="Pack JavaScript code into a single bundle",
      ~sdocs,
      ~exits,
    ),
  );
};

let all = [
  Build.command,
  Watch.command,
  Serve.command,
  Help.command,
  ParsingServer.command,
];
let default = Default.command;
