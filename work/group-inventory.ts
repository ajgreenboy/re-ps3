// Sub-page item inventory for the console (System), power, print and chat groups.
import { readFileSync } from "node:fs";
const db = JSON.parse(readFileSync("C:/dev/xmb/out/native-reference/rco-behavior/resources.json", "utf8"));
const r = db.resources.find((x: any) => x.path.includes("sysconf_plugin.rco"));
const objs = r.objects as any[];
const leafItems = (page: any) => {
  const out: string[] = [];
  const walk = (parent: string) => {
    for (const o of objs) if ((o.parent || "") === parent) {
      if (o.schema === "MItem") out.push(o.name);
      walk(o.name);
    }
  };
  walk(page.name);
  return out;
};
const groups = ["page_console_", "page_print_config", "page_chat_config", "page_sound_config", "page_network_config", "page_theme_config", "page_power"];
for (const g of groups) {
  const pages = objs.filter((o: any) => o.schema === "Page" && o.name.startsWith(g));
  console.log(`\n## ${g} -> ${pages.length} pages`);
  for (const p of pages) {
    const items = leafItems(p);
    console.log(`  ${p.name}${items.length ? "  :: " + items.join(" ") : ""}`);
  }
}
