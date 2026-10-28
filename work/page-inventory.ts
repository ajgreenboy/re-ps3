// Page inventory of sysconf_plugin.rco: top settings pages, their sub-pages and choice items.
// Reads out/native-reference/rco-behavior/resources.json only.
import { readFileSync } from "node:fs";
const db = JSON.parse(readFileSync("C:/dev/xmb/out/native-reference/rco-behavior/resources.json", "utf8"));
const r = db.resources.find((x: any) => x.path.includes("sysconf_plugin.rco"));
const objs = r.objects as any[];
const byParent = new Map<string, any[]>();
for (const o of objs) {
  const k = o.parent || "";
  if (!byParent.has(k)) byParent.set(k, []);
  byParent.get(k)!.push(o);
}
const desc = (name: string): any[] => (byParent.get(name) ?? []).flatMap((c: any) => [c, ...desc(c.name)]);
const tops = ["page_game_config", "page_bdvd_config", "page_music_config", "page_chat_config",
  "page_console_config", "page_theme_config", "page_date_time_config", "page_device_config",
  "page_print_config", "page_display_config", "page_sound_config", "page_security_config",
  "page_remote_config", "page_network_config"];
const power = objs.filter((o: any) => o.schema === "Page" && /auto_power|auto_off|power_sav/i.test(o.name)).map((o: any) => o.name);
console.log("power candidates:", power.join(", "), "\n");
for (const t of tops) {
  const kids = objs.filter((o: any) => o.schema === "Page" && o.name !== t && o.name.startsWith(t));
  console.log(`\n## ${t}  (sub-pages ${kids.length})`);
  for (const k of kids) {
    const items = desc(k.name).filter((o: any) => o.schema === "MItem").map((o: any) => o.name);
    const texts = desc(k.name).filter((o: any) => o.schema === "Text").map((o: any) => o.name);
    const ilists = desc(k.name).filter((o: any) => o.schema === "IList" || o.schema === "IItem").map((o: any) => o.name);
    const bits = [
      items.length ? `MItem[${items.length}]: ${items.join(" ")}` : "",
      texts.length ? `Text: ${texts.join(" ")}` : "",
      ilists.length ? `IList: ${ilists.join(" ")}` : "",
    ].filter(Boolean).join("  |  ");
    console.log(`  - ${k}${bits ? "  ::  " + bits : "  ::  (no items)"}`);
  }
}
