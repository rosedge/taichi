#include <taichi/taichi>
#include <set>
#include "../ir.h"

TLANG_NAMESPACE_BEGIN

namespace irpass {

class Offloader {
 public:
  Offloader(IRNode *root) {
    run(root);
  }

  void run(IRNode *root) {
    auto root_block = dynamic_cast<Block *>(root);
    auto root_statements = std::move(root_block->statements);
    root_block->statements.clear();

    auto pending_serial_statements =
        Stmt::make_typed<OffloadedStmt>(OffloadedStmt::TaskType::serial);

    auto assemble_serial_statements = [&]() {
      if (!pending_serial_statements->body_block->statements.empty()) {
        root_block->insert(std::move(pending_serial_statements));
        pending_serial_statements =
            Stmt::make_typed<OffloadedStmt>(OffloadedStmt::TaskType::serial);
      }
    };

    for (int i = 0; i < (int)root_statements.size(); i++) {
      auto &stmt = root_statements[i];
      if (auto s = stmt->cast<RangeForStmt>()) {
        assemble_serial_statements();
        auto offloaded =
            Stmt::make_typed<OffloadedStmt>(OffloadedStmt::TaskType::range_for);
        offloaded->body_block = std::make_unique<Block>();
        offloaded->begin = s->begin->as<ConstStmt>()->val[0].val_int32();
        offloaded->end = s->end->as<ConstStmt>()->val[0].val_int32();
        offloaded->block_size = s->block_size;
        auto loop_var = s->loop_var;
        replace_statements_with(
            s,
            [&](Stmt *load) {
              if (auto local_load = load->cast<LocalLoadStmt>()) {
                return local_load->width() == 1 &&
                       local_load->ptr[0].var == loop_var &&
                       local_load->ptr[0].offset == 0;
              }
              return false;
            },
            []() { return Stmt::make<LoopIndexStmt>(0); });
        for (int j = 0; j < (int)s->body->statements.size(); j++) {
          offloaded->body_block->insert(std::move(s->body->statements[j]));
        }
        root_block->insert(std::move(offloaded));
      } else if (auto s = stmt->cast<StructForStmt>()) {
        emit_struct_for(s, root_block);
        // TODO: emit listgen
        /*
      } else {
        // Serial stmt
        auto offloaded =
            Stmt::make_typed<OffloadedStmt>(OffloadedStmt::TaskType::serial);
        offloaded->body_stmt = std::move(root_statements[i]);
        new_root_statements.push_back(std::move(offloaded));
         */
      } else {
        pending_serial_statements->body_block->insert(std::move(stmt));
      }
    }
    assemble_serial_statements();
  }

  void emit_struct_for(StructForStmt *for_stmt, Block *root_block) {
    auto leaf = for_stmt->snode;
    TC_ASSERT(leaf->type == SNodeType::place)
    auto leaf_block = leaf->parent;

    // make a list of nodes, from the leaf block (instead of 'place') to root
    std::vector<SNode *> path;
    // leaf is the place (scalar)
    // leaf->parent is the leaf block
    // so listgen should be invoked from the root to leaf->parent->parent
    for (auto p = leaf->parent->parent; p; p = p->parent) {
      path.push_back(p);
    }
    std::reverse(path.begin(), path.end());

    for (int i = 0; i + 1 < path.size(); i++) {
      auto snode_child = path[i + 1];
      auto offloaded_listgen =
          Stmt::make_typed<OffloadedStmt>(OffloadedStmt::TaskType::listgen);
      offloaded_listgen->snode = snode_child;
      root_block->insert(std::move(offloaded_listgen));
    }

    auto offloaded_struct_for =
        Stmt::make_typed<OffloadedStmt>(OffloadedStmt::TaskType::struct_for);

    for (int i = 0; i < (int)for_stmt->body->statements.size(); i++) {
      offloaded_struct_for->body_block->insert(
          std::move(for_stmt->body->statements[i]));
    }

    root_block->insert(std::move(offloaded_struct_for));
  }
};

void offload(IRNode *root) {
  Offloader _(root);
  irpass::typecheck(root);
  irpass::fix_block_parents(root);
}

}  // namespace irpass

TLANG_NAMESPACE_END