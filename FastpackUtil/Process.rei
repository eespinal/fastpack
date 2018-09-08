type t

let start: string => t;
let withProcess:
  (t, (Lwt_io.channel(Lwt_io.input), Lwt_io.channel(Lwt_io.output)) => 'a)
  => 'a;
let withProcessAsync:
  (t,
    (Lwt_io.channel(Lwt_io.input), Lwt_io.channel(Lwt_io.output)) => Lwt.t('a))
  => Lwt.t('a)
let finalize: t => unit;

