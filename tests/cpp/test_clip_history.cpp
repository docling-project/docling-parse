// SPDX-License-Identifier: MIT
// Persistent clip history: prefix/branch isolation, traversal, translation and
// nonrecursive destruction. Run through CTest; checks remain active in Release.
#include <parse.h>

#include <stdexcept>
#include <unordered_set>

namespace
{
using namespace pdflib;

void require(bool condition, const std::string& message)
{
  if(not condition) { throw std::runtime_error(message); }
}

clip_path_instruction rectangle(int group, double x = 0.0)
{
  return clip_path_instruction({x,x+10,x+10,x,x}, {0,0,10,10,0}, CLOSED, RECTANGLE, group);
}

struct storage_totals
{
  std::unordered_set<const void*> nodes;
  std::size_t paths = 0;
  void add(const clip_state_instruction& state)
  {
    state.get_paths().visit_storage([&](const void* identity, std::size_t path_count,
                                       std::size_t, std::size_t)
      {
        if(not nodes.insert(identity).second) { return false; }
        paths += path_count;
        return true;
      });
  }
};

void snapshot_and_branch_isolation()
{
  clip_state_instruction empty;
  const auto first = empty.appended(CLIP_RULE_NONZERO, {rectangle(0)});
  const auto left = first.appended(CLIP_RULE_NONZERO, {rectangle(1, 20)});
  const auto right = first.appended(CLIP_RULE_EVEN_ODD, {rectangle(1, 40), rectangle(1, 60)});
  require(empty.get_paths().empty() and first.get_paths().size()==1,
          "appending changed an earlier snapshot");
  require(left.get_paths().size()==2 and right.get_paths().size()==3,
          "branches did not retain separate histories");
  const auto values = right.get_paths().to_vector();
  require(values[0].get_x()[0]==0 and values[1].get_x()[0]==40 and values[2].get_x()[0]==60,
          "forward traversal lost capture/subpath order");
  require(values[1].get_clip_group()==values[2].get_clip_group(), "capture grouping changed");
  require(first.get_rule()==CLIP_RULE_NONZERO and right.get_rule()==CLIP_RULE_EVEN_ODD,
          "a later rule changed a previous snapshot");
  require(first.with_rule(CLIP_RULE_EVEN_ODD).shares_history(first), "rule copy cloned geometry");
  storage_totals totals;
  for(const auto& state : {first, left, right}) { totals.add(state); }
  require(totals.nodes.size()==3 and totals.paths==4, "branch prefixes were not shared");
}

std::vector<clip_state_instruction> snapshots(std::size_t count)
{
  std::vector<clip_state_instruction> result;
  result.reserve(count);
  clip_state_instruction current;
  for(std::size_t i=0; i<count; ++i)
    {
      current = current.appended(CLIP_RULE_NONZERO, {rectangle(static_cast<int>(i), double(i))});
      result.push_back(current);
    }
  return result;
}

void linear_storage_and_forward_iterator()
{
  const auto states = snapshots(1000);
  storage_totals totals;
  std::size_t logical_paths = 0;
  for(const auto& state : states)
    {
      logical_paths += state.get_paths().size();
      totals.add(state);
    }
  require(logical_paths==500500, "logical clip occurrences changed");
  require(totals.nodes.size()==1000 and totals.paths==1000, "unique storage is not linear");
  const auto& paths = states.back().get_paths();
  require(paths.point_count()==5000, "wrong cumulative point count");
  auto copy = paths.begin();
  auto advanced = copy;
  ++advanced;
  require(copy->get_clip_group()==0 and advanced->get_clip_group()==1,
          "copying/advancing a forward iterator changed the original");
  int expected = 0;
  for(const auto& path : paths) { require(path.get_clip_group()==expected++, "reversed history traversal"); }
  require(expected==1000, "history traversal omitted a capture");
}

void require_cursor_matches(const clip_path_cursor& cursor,
                            const clip_state_instruction& state)
{
  const auto& paths = state.get_paths();
  require(cursor.size()==paths.size(), "cursor retained the wrong number of paths");
  auto expected = paths.begin();
  for(const auto& path : cursor)
    {
      require(expected!=paths.end() and &path==&*expected,
              "cursor copied, reordered or retained stale geometry");
      ++expected;
    }
  require(expected==paths.end(), "cursor omitted a clip path");
}

void cursor_extends_history_and_reuses_geometry()
{
  clip_path_cursor cursor;
  clip_state_instruction empty;
  const auto first = empty.appended(CLIP_RULE_NONZERO, {rectangle(0)});
  const auto second = first.appended(CLIP_RULE_NONZERO, {rectangle(1,20), rectangle(1,30)});
  const auto third = second.appended(CLIP_RULE_NONZERO, {rectangle(2,40)});
  // A later capture can reuse an existing group (for example a shading BBox).
  const auto fourth = third.appended(CLIP_RULE_NONZERO, {rectangle(0,50)});

  cursor.prepare(empty.get_paths());
  require(cursor.empty() and cursor.group_size(0)==0, "empty cursor has geometry/counts");
  cursor.prepare(first.get_paths());
  require_cursor_matches(cursor, first);
  cursor.prepare(second.get_paths());
  require_cursor_matches(cursor, second);
  require(cursor.group_size(0)==1 and cursor.group_size(1)==2,
          "single-capture extension lost subpath groups");
  // Skip the third snapshot: replay need not paint after every capture.
  cursor.prepare(fourth.get_paths());
  require_cursor_matches(cursor, fourth);
  require(cursor.group_size(0)==2 and cursor.group_size(1)==2 and cursor.group_size(2)==1,
          "multi-capture extension did not update existing/new groups");

  auto copy = cursor.begin();
  auto advanced = copy;
  ++advanced;
  require(copy->get_x()[0]==0 and advanced->get_x()[0]==20,
          "cursor iterator copies are not independent");
  const auto rule_change = fourth.with_rule(CLIP_RULE_EVEN_ODD);
  cursor.prepare(rule_change.get_paths());
  require_cursor_matches(cursor, fourth);
  require(cursor.group_size(0)==2, "rule-only change invalidated geometry groups");
}

void cursor_restores_branches_and_handles_translation()
{
  clip_path_cursor cursor;
  clip_state_instruction empty;
  const auto root = empty.appended(CLIP_RULE_NONZERO, {rectangle(0)});
  const auto left = root.appended(CLIP_RULE_NONZERO, {rectangle(1,20)});
  const auto right = root.appended(CLIP_RULE_NONZERO, {rectangle(0,40), rectangle(0,60)});
  cursor.prepare(left.get_paths());
  require_cursor_matches(cursor, left);
  cursor.prepare(root.get_paths());
  require_cursor_matches(cursor, root);
  require(cursor.group_size(0)==1 and cursor.group_size(1)==0,
          "restoring an ancestor retained child groups");
  cursor.prepare(left.get_paths());
  cursor.prepare(right.get_paths());
  require_cursor_matches(cursor, right);
  require(cursor.group_size(0)==3 and cursor.group_size(1)==0,
          "switching siblings retained the previous branch's groups");

  const auto shifted = right.translated(10,20);
  cursor.prepare(shifted.get_paths());
  require_cursor_matches(cursor, shifted);
  require(cursor.begin()->get_x()[0]==10 and cursor.begin()->get_y()[0]==20,
          "translated history reused unshifted coordinates");
  const clip_state_instruction independent(CLIP_RULE_NONZERO, {rectangle(9,100)});
  cursor.prepare(independent.get_paths());
  require_cursor_matches(cursor, independent);
  require(cursor.group_size(9)==1 and cursor.group_size(0)==0,
          "independent history retained old groups");
  cursor.prepare(empty.get_paths());
  require_cursor_matches(cursor, empty);
  require(cursor.group_size(9)==0, "returning to an empty history retained group counts");
  cursor.prepare(right.get_paths());
  cursor.reset();
  require(cursor.empty() and cursor.group_size(0)==0, "page reset retained cursor state");
  cursor.prepare(left.get_paths());
  require_cursor_matches(cursor, left);
}

void cursor_owns_referenced_history()
{
  clip_path_cursor cursor;
  const clip_path_instruction* original = nullptr;
  {
    const clip_state_instruction temporary(CLIP_RULE_NONZERO, {rectangle(7,70)});
    original = &*temporary.get_paths().begin();
    cursor.prepare(temporary.get_paths());
  }
  require(cursor.size()==1 and &*cursor.begin()==original and cursor.begin()->get_x()[0]==70,
          "cursor references outlived their history owner");
  cursor.reset();
  require(cursor.empty() and cursor.begin()==cursor.end(), "reset left a traversable history");
}

void translated_prefixes_stay_shared()
{
  const auto source = snapshots(64);
  clip_translation_cache cache;
  std::vector<clip_state_instruction> translated;
  storage_totals totals;
  for(const auto& state : source)
    {
      translated.push_back(state.translated(10, 20, &cache));
      totals.add(translated.back());
    }
  require(totals.nodes.size()==64 and totals.paths==64, "translation duplicated shared prefixes");
  require(source.front().get_paths().begin()->get_x()[0]==0, "translation changed source coordinates");
  require(translated.front().get_paths().begin()->get_x()[0]==10 and
          translated.front().get_paths().begin()->get_y()[0]==20, "wrong translated coordinates");
  require(source.back().translated(10,20,&cache).shares_history(translated.back()),
          "translation cache did not reuse the completed history");
  const auto shifted_again = source.front().translated(30,40,&cache);
  require(shifted_again.get_paths().begin()->get_x()[0]==30 and
          translated.front().get_paths().begin()->get_x()[0]==10,
          "changing the translation reused stale coordinates");
}

struct recording_renderer
{
  std::vector<clip_state_instruction> clips;
  void render_shape(const shape_instruction& shape) { clips.push_back(shape.get_clip_state()); }
  void render_text(const text_instruction&) {}
  void render_widget(const text_widget_instruction&) {}
  void render_bitmap(const bitmap_instruction&) {}
  void render_shading(const shading_instruction&) {}
};

void shifted_instruction_replay_uses_one_translation_cache()
{
  pdf_render_instructions instructions;
  const auto states = snapshots(32);
  for(const auto& state : states)
    {
      instructions.add_shape_instruction(shape_instruction({}, SHAPE_PAINT_FILL,
        SHAPE_FILL_NONZERO, 1, 0, 0, 10, {}, 0, {0,0,0}, {0,0,0}, 1, 1, state));
    }
  recording_renderer renderer;
  instructions.iterate_over_content(renderer, 10, 20);
  storage_totals totals;
  for(const auto& state : renderer.clips) { totals.add(state); }
  require(renderer.clips.size()==32 and totals.paths==32,
          "shifted instruction replay rematerialized clip prefixes");
}

void deep_unique_suffix_destroys_iteratively()
{
  clip_state_instruction history;
  for(int i=0; i<50000; ++i) { history = history.appended(CLIP_RULE_NONZERO, {rectangle(i)}); }
  require(history.get_paths().size()==50000, "deep history was truncated");
  history = clip_state_instruction();
  require(not history.has_clip(), "deep history release failed");
}
}

int main()
{
  try
    {
      snapshot_and_branch_isolation();
      linear_storage_and_forward_iterator();
      cursor_extends_history_and_reuses_geometry();
      cursor_restores_branches_and_handles_translation();
      cursor_owns_referenced_history();
      translated_prefixes_stay_shared();
      shifted_instruction_replay_uses_one_translation_cache();
      deep_unique_suffix_destroys_iteratively();
      std::cout << "Shared clip history checks passed\n";
    }
  catch(const std::exception& error)
    { std::cerr << error.what() << "\n"; return 1; }
  return 0;
}
