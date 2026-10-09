export function activate(host) {
  const { createElement: h, useEffect, useState } = host.React;
  function Logic({ context }) {
    const [text, setText] = useState("[]");
    const [actions, setActions] = useState([]);
    const [busy, setBusy] = useState(false);
    useEffect(() => {
      let disposed = false;
      void host.request("rules").then(async (response) => {
        if (!response.ok) throw new Error("Unable to load automations");
        const result = await response.json();
        if (!disposed) { setText(JSON.stringify(result.rules, null, 2)); setActions(result.actions); }
      }).catch((error) => host.notify("error", error.message));
      return () => { disposed = true; };
    }, []);
    const save = async () => {
      setBusy(true);
      try {
        const rules = JSON.parse(text);
        const response = await host.request("rules", {method:"PUT",headers:{"Content-Type":"application/json"},body:JSON.stringify(rules)});
        if (!response.ok) throw new Error((await response.json()).detail ?? "Unable to save automations");
        host.notify("success", "Automations saved");
      } catch (error) { host.notify("error", error.message); }
      finally { setBusy(false); }
    };
    return h("section", {style:{maxWidth:900,margin:"auto"}},
      h("div", {style:{display:"flex",alignItems:"center",justifyContent:"space-between",marginBottom:12}},
        h("h1", {style:{fontSize:24,fontWeight:700}}, "Logic"),
        context.auth.user?.role === "admin" && h("button", {disabled:busy,onClick:() => void save(),style:{padding:"8px 12px",border:"1px solid #525252",borderRadius:6}}, "Save")),
      h("textarea", {value:text,readOnly:context.auth.user?.role !== "admin",onChange:(event) => setText(event.target.value),"aria-label":"Automation rules",spellCheck:false,style:{width:"100%",minHeight:340,padding:12,background:"transparent",border:"1px solid #404040",borderRadius:8,fontFamily:"monospace"}}),
      h("div", {style:{fontSize:12,opacity:.6,marginTop:8}}, actions.join(" · ")));
  }
  host.module({id:"logic",label:"Logic",order:20,component:Logic});
}
