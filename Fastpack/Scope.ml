module S = Ast.Statement
module F = Ast.Function
module P = Ast.Pattern
module L = Ast.Literal
module M = Map.Make(String)

type t = {
  parent : t option;
  bindings : binding M.t;
}
and binding = Import of import
            | Function
            | Argument
            | Class
            | Var
            | Let
            | Const
and import = {
  source : string;
  remote: string option;
}

let empty = { bindings = M.empty; parent = None; }

let scope_to_str ?(sep="\n") scope =
  scope.bindings
  |> M.bindings
  |> List.map
    (fun (name, typ) ->
       name ^ " -> " ^
       (match typ with
        | Import { source; remote } ->
          let remote = (match remote with | None -> "*" | Some n -> n) in
          Printf.sprintf "Import %s from '%s'" remote source
        | Function -> "Function"
        | Class -> "Class"
        | Argument -> "Argument"
        | Var -> "Var"
        | Let -> "Let"
        | Const -> "Const"
       )
    )
  |> String.concat sep

let name_of_identifier (_, name) =
  name

let names_of_pattern node =
  let rec names_of_pattern' names (_, node) =
    match node with
    | P.Object { properties; _ } ->
      let on_property names = function
        | P.Object.Property (_,{ key; pattern; shorthand }) ->
          if shorthand then
            match key with
            | P.Object.Property.Identifier id -> (name_of_identifier id)::names
            | _ -> names
          else
            names_of_pattern' names pattern
        | P.Object.RestProperty (_,{ argument }) ->
          names_of_pattern' names argument
      in
      List.fold_left on_property names properties
    | P.Array { elements; _ } ->
      let on_element names = function
        | None ->
          names
        | Some (P.Array.Element node) ->
          names_of_pattern' names node
        | Some (P.Array.RestElement (_, { argument })) ->
          names_of_pattern' names argument
      in
      List.fold_left on_element names elements
    | P.Assignment { left; _ } ->
      names_of_pattern' names left
    | P.Identifier { name = id; _ } ->
      (name_of_identifier id)::names
    | P.Expression _ ->
      names
  in names_of_pattern' [] node

let update_bindings name typ bindings =
  match M.get name bindings, typ with
  | None, _
  | Some Argument, _
  | Some Function, Var
  | Some Function, Function
  | Some Var, Var ->
    M.add name typ bindings
  | Some Var, Function ->
    bindings
  | _ -> (* TODO: track the Loc.t of bindings and raise the nice error *)
    failwith ("Naming collision: " ^ name)


let collect_declarations kind add =
  let typ =
    match kind with
    | S.VariableDeclaration.Let -> Let
    | S.VariableDeclaration.Const -> Const
    | S.VariableDeclaration.Var -> Var
  in
  List.iter
    (fun (_, {S.VariableDeclaration.Declarator. id; _ }) ->
       List.iter (add typ) (names_of_pattern id)
    )

let of_statement stmt scope =
  let bindings = ref (M.empty) in
  let add_binding typ name =
    bindings := update_bindings name typ !bindings
  in
  let () =
    match stmt with
    | S.For { init = Some (S.For.InitDeclaration (_, { declarations; kind })); _ }
      when kind = S.VariableDeclaration.Let
        || kind = S.VariableDeclaration.Const ->
      collect_declarations kind add_binding declarations;
    | S.ForIn { left = S.ForIn.LeftDeclaration (_, { declarations; kind }); _ }
      when kind = S.VariableDeclaration.Let
        || kind = S.VariableDeclaration.Const ->
      collect_declarations kind add_binding declarations;
    | S.ForOf { left = S.ForOf.LeftDeclaration (_, { declarations; kind }); _ }
      when kind = S.VariableDeclaration.Let
        || kind = S.VariableDeclaration.Const ->
      collect_declarations kind add_binding declarations;
    | S.Block { body } ->
      List.iter
        (fun (_, stmt) ->
           match stmt with
           | S.ClassDeclaration { id = Some (_, name); _} ->
             add_binding Class name
           | S.VariableDeclaration { kind; declarations }
             when kind = S.VariableDeclaration.Let
               || kind = S.VariableDeclaration.Const ->
             collect_declarations kind add_binding declarations
           | _ -> ()
        )
        body
    | _ -> ()
  in
  if !bindings != M.empty
  then { bindings = !bindings; parent = Some scope; }
  else scope


let of_function_body args stmts scope =
  let bindings =
    ref @@ List.fold_left (fun m key -> M.add key Argument m) M.empty args
  in

  let add_binding typ name =
    bindings := update_bindings name typ !bindings
  in

  let level = ref 0 in

  let enter_statement _ =
    level := !level + 1
  in

  let leave_statement _ =
    level := !level - 1
  in

  let visit_statement ((_, stmt) : S.t) =
    match stmt with
    | S.ImportDeclaration {
        importKind = S.ImportDeclaration.ImportValue;
        source = (_, { value = L.String source; _ });
        specifiers
      } ->
      List.iter
        (fun spec ->
           match spec with
           | S.ImportDeclaration.ImportNamedSpecifier { local; remote; _ } ->
             let local =
               match local with
               | Some name -> name
               | None -> remote
             in add_binding
               (Import {remote = Some (name_of_identifier remote); source})
               (name_of_identifier local)
           | S.ImportDeclaration.ImportDefaultSpecifier (_, name) ->
             add_binding (Import { remote = Some "default"; source }) name
           | S.ImportDeclaration.ImportNamespaceSpecifier (_, (_, name)) ->
             add_binding (Import { remote = None; source }) name

        )
        specifiers;
      Visit.Break;

    | S.ClassDeclaration { id; _} ->
      let () =
        match id, !level with
        | Some (_, name), 1 -> add_binding Class name
        | _ -> ()
      in Visit.Break
    | S.FunctionDeclaration { id; _ } ->
      let () =
        match id with
        | Some (_, name) -> add_binding Function name
        | None -> ()
      in Visit.Break
    | S.VariableDeclaration { kind; declarations } ->
      let () =
        match kind, !level with
        | S.VariableDeclaration.Let, 1
        | S.VariableDeclaration.Const, 1
        | S.VariableDeclaration.Var, _ ->
          collect_declarations kind add_binding declarations
        | _ -> ()
      in Visit.Break
    | S.For { init = Some (S.For.InitDeclaration (_, {
        declarations;
        kind = S.VariableDeclaration.Var
      })); _ } ->
      collect_declarations S.VariableDeclaration.Var add_binding declarations;
      Visit.Continue
    | S.ForIn { left = S.ForIn.LeftDeclaration (_, {
        declarations;
        kind = S.VariableDeclaration.Var;
      }); _ } ->
      collect_declarations S.VariableDeclaration.Var add_binding declarations;
      Visit.Continue
    | S.ForOf { left = S.ForOf.LeftDeclaration (_, {
        declarations;
        kind = S.VariableDeclaration.Var;
      }); _ } ->
      collect_declarations S.VariableDeclaration.Var add_binding declarations;
      Visit.Continue
    | _ -> Visit.Continue
  in

  let handler = {
    Visit.
    visit_statement;
    visit_expression = (fun _ -> Visit.Break);
    visit_pattern = (fun _ -> Visit.Break);
    visit_function = (fun _ -> Visit.Break);
    enter_statement;
    leave_statement;
  } in
  let () =
    Visit.visit_list handler Visit.visit_statement stmts
  in {
    bindings = !bindings;
    parent = Some scope;
  }

let of_function (_, {F. params; body; _}) =
  let params, rest = params in
  let arguments =
    List.flatten
    @@ List.append
      (List.map names_of_pattern params)
      (match rest with
       | Some (_, { F.RestElement.  argument }) ->
         [names_of_pattern argument]
       | None -> []
      )
  in
  match body with
  | F.BodyBlock (_, { body; }) -> of_function_body arguments body
  | F.BodyExpression _ -> of_function_body arguments []

let of_program = of_function_body []

let rec get_binding name { bindings; parent } =
  let binding = M.get name bindings in
  match binding, parent with
  | None, Some parent -> get_binding name parent
  | _ -> binding

let has_binding name scope =
  (get_binding name scope) != None
