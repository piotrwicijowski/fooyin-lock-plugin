# Widget discovery and input-rule infrastructure

Ticket [006](../.plan/006-discover-widgets-and-filter-input.md), verified on Linux,
Qt 6.8.2, fooyin 0.13.1 commit `bb8d9f0371b254571fffec310dce3ee1ce005c87`.

`WidgetInputFilter` provides dispatch-time discovery and reusable input candidates.
`PlaylistGuard` uses it for playlist rules; `PlaybackGuard` consumes activation and
drop candidates, and ActionGuard discovers direct actions before root/menu input.
See the [production playback policy](playback-protection.md). `SeekGuard` consumes
seek candidates and cancels previews; see [seeking protection](seeking-protection.md).
Search-write candidates and settings-side-effect handling remain integration points for 009.

## Identification matrix

Identification uses `QObject::inherits()` with fully qualified metaobject class
names. Neither translated labels nor object names are identities. No private
headers are compiled into the plugin. These runtime class/structure assumptions
are source-sensitive and must be re-audited for a new host baseline.

| Surface | Audited runtime root | Relevant descendants / assumptions |
| --- | --- | --- |
| Playlist | `Fooyin::PlaylistWidget` | Main and detached playlist views, item-view viewport and header. |
| Tabs | `Fooyin::PlaylistTabs` | Tab bar and corner/close buttons; nested content has its own surface. |
| Switcher | `Fooyin::PlaylistBox` | Combobox/popup and switcher menus. |
| Manager | `Fooyin::PlaylistManagerWidget` | Item view, viewport, inline editors. |
| Organiser | `Fooyin::PlaylistOrganiser` | Playlist tree and viewport. |
| Queue | `Fooyin::QueueViewer` | `Fooyin::QueueViewerView` / viewport, header, scrollbars. |
| Library tree | `Fooyin::LibraryTreeWidget` | `Fooyin::LibraryTreeView` / viewport; embedded search is a separate surface. |
| Filter | `Fooyin::Filters::FilterWidget` | Filter item view / viewport, header and scrollbars. |
| Search | `Fooyin::SearchWidget` | Search bar / quick-search and embedded search, with a line editor. Search mode is configuration, not a different runtime type. |
| Seekbar | `Fooyin::SeekBar` | Only `Fooyin::TrackSlider` is a seek control; time labels/container are separate. |
| Wavebar | `Fooyin::WaveBar::WaveBarWidget` | Only `Fooyin::WaveBar::WaveSeekBar` is a seek control. |
| Spectrogram | `Fooyin::Spectrogram::SpectrogramWidget` | Full-track mode creates `Fooyin::Spectrogram::SpectrogramSeekBar`. Scrolling/stationary modes create `Fooyin::Spectrogram::LiveSpectrogramView`, which is not a seek control. Mode changes replace the subtree. |

The nearest recognized root wins. An unrecognized `Fooyin::FyWidget` stops the
ancestry walk with `Unknown`. Thus library/search, a detached playlist inside a
filter Track Viewer, and third-party content inside tabs keep their own identity;
outer tab rules cannot leak into them. Track Viewer itself is an unknown container;
its actual nested `PlaylistWidget` has the playlist surface. Generic QWidget
wrappers and parented menus can resolve to their owning surface. Unparented popups
and unrelated widgets are unknown; no guessed owner or blanket protection is
claimed. Unknown surfaces are left to the host by production rules and the
test consumer.

`role()` independently identifies root, item view, viewport descendants, header,
scrollbar, text editor (`QLineEdit`, `QTextEdit`, `QPlainTextEdit`), menu and the three
named seek controls. Ancillary children outside a view's viewport are `Other`.
Editors take precedence over their containing view. A surface alone is not enough
to justify blocking an event.

## Rule contract

The scoped application filter runs before receiver event filters, widget handlers,
view activation signals and resulting controller calls. It invokes the supplied
rule only for mouse press/release/double-click/move, wheel, key press/release/
shortcut override, drag enter/move/leave/drop and context-menu events. Painting,
timers, construction/destruction, model updates and playback notifications bypass
the rule. A `false` result leaves delivery unchanged. A consumed event is accepted,
including `ShortcutOverride`; consumed drag enter/move/drop is instead ignored so
Qt does not advertise drop acceptance.

Reusable predicates are deliberately input **candidates**, not the final lock
policy:

| Predicate | Matches | Leaves available |
| --- | --- | --- |
| `isKeyInput` | Key press, release and shortcut override. | Other event types. |
| `isActivation` | Playlist/queue/library/filter root, view or viewport: Enter/Return (all key phases/modifiers), left double-click, middle press/release/double-click. | Left single-click selection, navigation, Space, ordinary typing; editors, menus, headers and scrollbars. |
| `isSearchSubmit` | Search root/editor Enter/Return, including force-new-playlist modifiers. | Text entry, copy/search shortcuts, Escape; other surfaces' editors. |
| `isSeekInput` | Named seek controls: left press/release/double-click/held move, wheel, arrow keys in all key phases; built-in QSlider also Home/End/PageUp/PageDown. | Hover, focus traversal, Escape/right-button cancellation, labels, spectrogram live views, volume controls. |
| `isMutationDrop` | Drag enter/move/drop; consumer chooses the destination surface. | Drag leave (allows host cleanup); it does not inhibit drag initiation. |

Ticket 007 combines activation/drop candidates with direct menu-source protection
and drag-initiation restrictions. Library double-click expansion is separated
through public tree APIs; registered queue deletion/edit shortcuts remain covered
by ActionGuard. Allowing selection/text changes cannot suppress selection-generated
playlist writes or playback-follows-cursor. Those effects require ticket 009's
browsing guard. Ordinary keyboard expansion, selection, scrolling and editor input
remain deliverable.

## Dynamic discovery and lifetime

There is no widget registry, constructor-time type snapshot, cached viewport or
polling timer. Each input resolves its receiver's current ancestry. Existing,
newly created, reparented, replaced and recreated surfaces therefore use the same
path on their first input, without an event-loop discovery window. A new
Spectrogram seekbar after a mode change follows this rule too.

Each guard owns its filter and calls `start()` during entry and `stop()` during
cleanup. Both are idempotent; the destructor detaches as a backstop. No host widget
or callback is retained by discovery. Guard lambdas capturing their owner must have
the same scoped lifetime. Multiple guards may own independent filters; Qt runs
application filters in reverse installation order. Required guards must be active
before the coordinator reports Locked. Other plugins' application-level filters
can run earlier and are outside the receiver-ordering guarantee.

The predicates match a protected release even if its press occurred before
installation. Predicates alone do not clear host gesture state or undo a request
already submitted. SeekGuard additionally sends the audited UngrabMouse cancellation
event at entry and before consuming protected input. Ticket 010 integrates the
assembled seek/drag transition lifecycle.

## Audit sources and verification

Source inspected in the detached pinned checkout:

- `src/gui/widgets.cpp`: provider keys, root construction, nested Track Viewer.
- `src/gui/queueviewer/{queueviewer,queueviewerview,queueviewermodel}.cpp`: direct
  queue activation/context commands, dragging and model drops.
- `src/gui/librarytree/{librarytreewidget,librarytreeview}.cpp`: Enter forwarding,
  configured double/middle click, expansion and selection side effects.
- `src/gui/filters/{filterwidget,filtercontroller}.cpp`: root Enter,
  double/middle-click connections and selection side effects.
- `src/gui/search/searchwidget.cpp`: editor, Return modifiers, timed search and
  per-mode behavior; ordinary typing must remain usable.
- `src/gui/controls/seekbar.cpp`,
  `src/plugins/wavebar/{wavebarwidget,waveseekbar}.cpp`,
  `src/plugins/spectrogram/{spectrogramwidget,spectrogramseekbar}.cpp`: actual seek
  controls, namespaces, input handlers and Spectrogram view replacement.

The Qt-only `lock.widget-input` executable uses isolated metaobject fixtures to
exercise identity/boundaries, event candidates, acceptance, idempotence, replacement,
reparenting, destruction and real QTreeView selection/expansion/editor behavior.
It does not link lookalike classes into a process using fooyin libraries.

The real-host driver separately creates actual queue/library/filter/search/seek
widgets before and after installing a test-only candidate consumer, replaces
viewports/subtrees and probes receiver delivery. It verifies input ordering,
selection/navigation/wheel/text preservation, ignored drops and restored delivery
after detaching, while the production playlist guard is locked. This also proves
playlist-specific restrictions do not leak into newly recognized surfaces. Optional
matching Wavebar/Spectrogram modules extend that check to both seek controls and
all three Spectrogram presentation modes. No audio-output or general third-party
widget compatibility is inferred from these infrastructure checks. See
[commands and results](verification.md#ticket-006-verification).
