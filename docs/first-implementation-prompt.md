# First implementation prompt

Implement milestone 1 of `docs/development-plan.md` in the `majklvi/Salamander-Everything-plugin` repository. Deliver a Windows x64 native C++ filesystem plugin that I can load in stock Open Samandarin 0.16. I want to type in a fixed search field, sort the results with one click, and immediately continue typing. Use deterministic in-memory data for this milestone. Everything IPC and real file actions come in later milestones.

## Build the smallest usable plugin

1. Read `README.md`, `docs/samandarin-integration.md` and `docs/panel-overlay.md`, especially “Moving the list down” and “First click on a column heading.” Pin the native SDK to the 0.16 source revision `7382688b9ba1c9471a27b19f36beba65825b2940`. Use the upstream DemoPlug exports, filesystem interfaces and x64 project as references. Keep the build standalone, make the SDK location configurable and document the exact build command and resulting `.spl` path. [Pinned SDK](https://github.com/KRtkovo-eu-AI/salamander/tree/7382688b9ba1c9471a27b19f36beba65825b2940/src/plugins/shared) · [DemoPlug filesystem example](https://github.com/KRtkovo-eu-AI/salamander/tree/7382688b9ba1c9471a27b19f36beba65825b2940/src/plugins/demoplug)
2. Add a plugin command that opens the read-only virtual results root in the active panel, using the filesystem name assigned by the host. Supply sample entries such as `invoice-a.pdf`, `invoice-b.pdf` and a folder called `notes`. A simple substring filter is sufficient. Publish the filtered snapshot through the normal filesystem listing and refresh path. Keep the editor alive as rows change.
3. In the main frame's Detailed view, move the plugin's own list down and shorten it by one row, preserving its bottom edge. Place a borderless, activatable modeless popup with a Unicode edit control in the free strip above the native header. Keep its owner enabled and let the header handle mouse input directly. Track the current panel geometry and apply the offset once per host layout. On view or tab changes, remove the strip when the binding is lost. On close or unload, release the popup and callbacks and restore the current host layout. Confine all private window handling to this plugin's results view.

## Verify the result in Samandarin

| Exercise | What to verify |
| --- | --- |
| Load the built plugin, invoke the command and type queries giving zero, one and several matches. | Filtering updates the panel. A single result is fully visible beneath the header. Text selection and the caret survive a refresh. |
| Type `invoic`. Click and release **once** on Name, check the order of the two invoices, then type `e` without clicking the editor. | The single click changes their order exactly once. The field now reads `invoice` and still shows both matches. Record where keyboard focus went. |
| Select part of the query, press Delete, paste text, type keypad `+`, then press F5 and F8 while editing. | Text editing works and the keys do not invoke host file commands. Check command dispatch if the read-only fixture makes the visible effect ambiguous. |
| Resize the panel repeatedly, drag the splitter, switch to the other panel, close the results view, then unload and reload the plugin. | The header remains usable. The offset does not accumulate, the editor never covers the other panel, and search can be reopened cleanly. |

Provide the code, a short English build/install guide and the results of the checks you actually ran. Report the host version, architecture, artifact path and any failed or unrun checks. If first-click sorting or continued typing fails, distinguish mouse capture, active window and keyboard focus. Do not synthesize a second click or force focus back after every refresh. If the host is unavailable, finish the build work you can verify and give the exact remaining in-host steps. Keep this implementation confined to the plugin repository and milestone 1.
