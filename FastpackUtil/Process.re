type t = {
  cmd: string,
  process: Lwt_process.process_none,
  fp_in_ch: Lwt_io.channel(Lwt_io.input),
  fp_out_ch: Lwt_io.channel(Lwt_io.output),
};

let start = cmd => {
  /* TODO: handle Unix_error */
  let (fp_in, process_out) = Unix.pipe();
  let (process_in, fp_out) = Unix.pipe();
  let fp_in_ch = Lwt_io.of_unix_fd(~mode=Lwt_io.Input, fp_in);
  let fp_out_ch = Lwt_io.of_unix_fd(~mode=Lwt_io.Output, fp_out);
  let process =
    Lwt_process.(
      open_process_none(
        ~env=Unix.environment(),
        ~stdin=`FD_move(process_in),
        ~stdout=`FD_move(process_out),
        ~stderr=`Dev_null,
        shell(cmd),
      )
    );
  let () =
    switch (process#state) {
    | Lwt_process.Running => ()
    | Lwt_process.Exited(_) => Error.ie("Cannot run process: " ++ cmd)
    };
  {cmd, process, fp_in_ch, fp_out_ch};
};

let withProcess = ({fp_in_ch, fp_out_ch, cmd, _}, f) =>
  try (f(fp_in_ch, fp_out_ch)) {
  | End_of_file => Error.ie("Process '" ++ cmd ++ "' is not running.")
  | exn => raise(exn)
  };

let withProcessAsync = ({fp_in_ch, fp_out_ch, cmd, _}, f) =>
  Lwt.catch(
    () => f(fp_in_ch, fp_out_ch),
    fun
    | End_of_file => Error.ie("Process '" ++ cmd ++ "' is not running.")
    | exn => raise(exn),
  );

let finalize = ({process, _}: t) => {
  process#terminate;
  process#close |> ignore;
};
