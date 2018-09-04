module M = Map.Make(String);
module StringSet = Set.Make(String);
exception Cycle(list(string));

type t = {
  modules: Hashtbl.t(Module.location, Lwt.t(Module.t)),
  static_dependencies:
    Hashtbl.t(Module.location, (Module.Dependency.t, Module.location)),
  dynamic_dependencies:
    Hashtbl.t(Module.location, (Module.Dependency.t, Module.location)),
  build_dependencies: Hashtbl.t(string, Module.location),
};

let empty = (~size=5000, ()) => {
  modules: Hashtbl.create(size),
  static_dependencies: Hashtbl.create(size * 20),
  dynamic_dependencies: Hashtbl.create(size * 20),
  build_dependencies: Hashtbl.create(size * 5),
};

let lookup = (table, key) => Hashtbl.find_opt(table, key);

let lookup_module = (graph, location) => lookup(graph.modules, location);

let lookup_dependencies = (~kind, graph, m: Module.t) => {
  let dependencies =
    switch (kind) {
    | `Static => Hashtbl.find_all(graph.static_dependencies, m.location)
    | `Dynamic => Hashtbl.find_all(graph.dynamic_dependencies, m.location)
    | `All =>
      Hashtbl.find_all(graph.static_dependencies, m.location)
      @ Hashtbl.find_all(graph.dynamic_dependencies, m.location)
    };

  List.map(
    ((dep, location)) => (dep, lookup_module(graph, location)),
    dependencies,
  );
};

let to_dependency_map = graph => {
  let to_pairs =
    Hashtbl.map_list((_, (dep, location)) =>
      switch (lookup_module(graph, location)) {
      | None => failwith("not good at all, unknown location")
      | Some(m) => (dep, m)
      }
    );

  Lwt_list.fold_left_s(
    (dep_map, (dep, m)) => {
      let%lwt m = m;
      Module.DependencyMap.add(dep, m, dep_map) |> Lwt.return;
    },
    Module.DependencyMap.empty,
    to_pairs(graph.static_dependencies)
    @ to_pairs(graph.dynamic_dependencies),
  );
};

let add_module = (graph, location, m: Lwt.t(Module.t)) => {
  switch (Hashtbl.find_all(graph.modules, location)) {
  | [] => Hashtbl.add(graph.modules, location, m)
  | [_] =>
    Hashtbl.remove(graph.modules, location);
    Hashtbl.add(graph.modules, location, m);
  | _ => failwith("DependencyGraph: cannot add more modules")
  };
  m;
};

let add_build_dependencies = (graph, filenames, location) =>
  List.iter(
    filename => Hashtbl.add(graph.build_dependencies, filename, location),
    filenames,
  );

let add_dependency =
    (~kind, graph, m: Module.t, dep: (Module.Dependency.t, Module.location)) => {
  let dependencies =
    switch (kind) {
    | `Static => graph.static_dependencies
    | `Dynamic => graph.dynamic_dependencies
    };
  Hashtbl.add(dependencies, m.location, dep);
};

let remove_module = (graph, location: Module.location) => {
  let remove = (k, v) =>
    if (Module.equal_location(k, location)) {
      None;
    } else {
      Some(v);
    };

  let remove_files = (_, location') =>
    if (Module.equal_location(location, location')) {
      None;
    } else {
      Some(location');
    };

  Hashtbl.filter_map_inplace(remove, graph.modules);
  Hashtbl.filter_map_inplace(remove, graph.static_dependencies);
  Hashtbl.filter_map_inplace(remove, graph.dynamic_dependencies);
  Hashtbl.filter_map_inplace(remove_files, graph.build_dependencies);
};

let get_files = graph =>
  Hashtbl.fold(
    (filename, _, set) => StringSet.add(filename, set),
    graph.build_dependencies,
    StringSet.empty,
  );

let get_changed_module_locations = (graph, filenames) =>
  List.fold_left(
    (locations, filename) =>
      List.fold_left(
        (locations, location) => Module.LocationSet.add(location, locations),
        locations,
        Hashtbl.find_all(graph.build_dependencies, filename),
      ),
    Module.LocationSet.empty,
    filenames,
  )

/* TODO: make emitted_modules be LocationSet */
let cleanup = (graph, emitted_modules) => {
  let keep = (location, value) =>
    if (Module.LocationSet.mem(location, emitted_modules)) {
      Some(value);
    } else {
      None;
    };

  let () = Hashtbl.filter_map_inplace(keep, graph.modules);
  let () = Hashtbl.filter_map_inplace(keep, graph.static_dependencies);
  let () = Hashtbl.filter_map_inplace(keep, graph.dynamic_dependencies);
  let () =
    Hashtbl.filter_map_inplace(
      (_, location) => keep(location, location),
      graph.build_dependencies,
    );
  graph;
};

let length = graph => Hashtbl.length(graph.modules);

let modules = graph => Hashtbl.to_seq(graph.modules);

/* let get_static_chain = (graph, entry) => { */
/*   let modules = ref([]); */
/*   let seen_globally = ref(StringSet.empty); */
/*   let add_module = (m: Module.t) => { */
/*     let location_str = Module.location_to_string(m.location); */
/*     modules := [m, ...modules^]; */
/*     seen_globally := StringSet.add(location_str, seen_globally^); */
/*   }; */

/*   let check_module = (m: Module.t) => { */
/*     let location_str = Module.location_to_string(m.location); */
/*     StringSet.mem(location_str, seen_globally^); */
/*   }; */

/*   let rec sort = (seen, m: Module.t) => { */
/*     let location_str = Module.location_to_string(m.location); */
/*     List.mem(location_str, seen) ? */
/*       /1* let prev_m = *1/ */
/*       /1*   match lookup_module graph (List.hd seen) with *1/ */
/*       /1*   | Some prev_m -> prev_m *1/ */
/*       /1*   | None -> Error.ie "DependencyGraph.sort - imporssible state" *1/ */
/*       /1* in *1/ */
/*       switch (m.module_type) { */
/*       | Module.ESM */
/*       | Module.CJS_esModule => () */
/*       | Module.CJS => raise(Cycle([location_str, ...seen])) */
/*       } : */
/*       check_module(m) ? */
/*         () : */
/*         { */
/*           let sort' = sort([location_str, ...seen]); */
/*           let () = */
/*             List.iter( */
/*               sort', */
/*               List.filter_map( */
/*                 ((_, m)) => m, */
/*                 lookup_dependencies(~kind=`Static, graph, m), */
/*               ), */
/*             ); */

/*           add_module(m); */
/*         }; */
/*   }; */

/*   sort([], entry); */
/*   List.rev(modules^); */
/* }; */
