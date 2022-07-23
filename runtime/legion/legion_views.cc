/* Copyright 2022 Stanford University, NVIDIA Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "legion.h"
#include "legion/runtime.h"
#include "legion/legion_ops.h"
#include "legion/legion_tasks.h"
#include "legion/region_tree.h"
#include "legion/legion_spy.h"
#include "legion/legion_profiling.h"
#include "legion/legion_instances.h"
#include "legion/legion_views.h"
#include "legion/legion_analysis.h"
#include "legion/legion_trace.h"
#include "legion/legion_context.h"
#include "legion/legion_replication.h"

namespace Legion {
  namespace Internal {

    LEGION_EXTERN_LOGGER_DECLARATIONS

    /////////////////////////////////////////////////////////////
    // LogicalView 
    /////////////////////////////////////////////////////////////

    //--------------------------------------------------------------------------
    LogicalView::LogicalView(RegionTreeForest *ctx, DistributedID did,
                             AddressSpaceID own_addr, bool register_now,
                             CollectiveMapping *map)
      : DistributedCollectable(ctx->runtime, did, own_addr, register_now, map),
        context(ctx)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    LogicalView::~LogicalView(void)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    /*static*/ void LogicalView::handle_view_request(Deserializer &derez,
                                        Runtime *runtime, AddressSpaceID source)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez);
      DistributedID did;
      derez.deserialize(did);
      DistributedCollectable *dc = runtime->find_distributed_collectable(did);
#ifdef DEBUG_LEGION
      LogicalView *view = dynamic_cast<LogicalView*>(dc);
      assert(view != NULL);
#else
      LogicalView *view = static_cast<LogicalView*>(dc);
#endif
      view->send_view(source);
    } 

    /////////////////////////////////////////////////////////////
    // InstanceView 
    ///////////////////////////////////////////////////////////// 

    //--------------------------------------------------------------------------
    InstanceView::InstanceView(RegionTreeForest *ctx, DistributedID did,
                               AddressSpaceID owner_sp, UniqueID own_ctx,
                               bool register_now, CollectiveMapping *mapping)
      : LogicalView(ctx, did, owner_sp, register_now, mapping),
        owner_context(own_ctx)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    InstanceView::~InstanceView(void)
    //--------------------------------------------------------------------------
    { 
    }

#ifdef ENABLE_VIEW_REPLICATION
    //--------------------------------------------------------------------------
    void InstanceView::process_replication_request(AddressSpaceID source,
                                                  const FieldMask &request_mask,
                                                  RtUserEvent done_event)
    //--------------------------------------------------------------------------
    {
      // Should only be called by derived classes
      assert(false);
    }

    //--------------------------------------------------------------------------
    void InstanceView::process_replication_response(RtUserEvent done_event,
                                                    Deserializer &derez)
    //--------------------------------------------------------------------------
    {
      // Should only be called by derived classes
      assert(false);
    }

    //--------------------------------------------------------------------------
    void InstanceView::process_replication_removal(AddressSpaceID source,
                                                  const FieldMask &removal_mask)
    //--------------------------------------------------------------------------
    {
      // Should only be called by derived classes
      assert(false);
    }
#endif // ENABLE_VIEW_REPLICATION 

    //--------------------------------------------------------------------------
    /*static*/ void InstanceView::handle_view_register_user(Deserializer &derez,
                                        Runtime *runtime, AddressSpaceID source)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez);
      DistributedID did;
      derez.deserialize(did);
      RtEvent ready;
      LogicalView *view = runtime->find_or_request_logical_view(did, ready);
      DistributedID target_did;
      derez.deserialize(target_did);
      RtEvent target_ready;
      PhysicalManager *target =
        runtime->find_or_request_instance_manager(target_did, target_ready);

      RegionUsage usage;
      derez.deserialize(usage);
      FieldMask user_mask;
      derez.deserialize(user_mask);
      IndexSpace handle;
      derez.deserialize(handle);
      IndexSpaceNode *user_expr = runtime->forest->get_node(handle);
      UniqueID op_id;
      derez.deserialize(op_id);
      size_t op_ctx_index;
      derez.deserialize(op_ctx_index);
      unsigned index;
      derez.deserialize(index);
      ApEvent term_event;
      derez.deserialize(term_event);
      RtEvent collect_event;
      derez.deserialize(collect_event);
      size_t local_collective_arrivals;
      derez.deserialize(local_collective_arrivals);
      ApUserEvent ready_event;
      derez.deserialize(ready_event);
      RtUserEvent applied_event;
      derez.deserialize(applied_event);
      const PhysicalTraceInfo trace_info = 
        PhysicalTraceInfo::unpack_trace_info(derez, runtime);

      if (ready.exists() && !ready.has_triggered())
        ready.wait();
      if (target_ready.exists() && !target_ready.has_triggered())
        target_ready.wait();
#ifdef DEBUG_LEGION
      assert(view->is_instance_view());
#endif
      InstanceView *inst_view = view->as_instance_view();
      std::set<RtEvent> applied_events;
      ApEvent pre = inst_view->register_user(usage, user_mask, user_expr,
                                             op_id, op_ctx_index, index,
                                             term_event, collect_event, 
                                             target, local_collective_arrivals,
                                             applied_events, trace_info,source);
      if (ready_event.exists())
        Runtime::trigger_event(&trace_info, ready_event, pre);
      if (!applied_events.empty())
        Runtime::trigger_event(applied_event, 
            Runtime::merge_events(applied_events));
      else
        Runtime::trigger_event(applied_event);
    } 

#ifdef ENABLE_VIEW_REPLICATION
    //--------------------------------------------------------------------------
    /*static*/ void InstanceView::handle_view_replication_request(
                   Deserializer &derez, Runtime *runtime, AddressSpaceID source)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez);
      DistributedID did;
      derez.deserialize(did);
      RtEvent ready = RtEvent::NO_RT_EVENT;
      LogicalView *view = runtime->find_or_request_logical_view(did, ready);

      FieldMask request_mask;
      derez.deserialize(request_mask);
      RtUserEvent done_event;
      derez.deserialize(done_event);
      
      if (ready.exists() && !ready.has_triggered())
        ready.wait();
#ifdef DEBUG_LEGION
      assert(view->is_instance_view());
#endif
      InstanceView *inst_view = view->as_instance_view();
      inst_view->process_replication_request(source, request_mask, done_event);
    }

    //--------------------------------------------------------------------------
    /*static*/ void InstanceView::handle_view_replication_response(
                                          Deserializer &derez, Runtime *runtime)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez);
      DistributedID did;
      derez.deserialize(did);
      RtEvent ready = RtEvent::NO_RT_EVENT;
      LogicalView *view = runtime->find_or_request_logical_view(did, ready);

      RtUserEvent done_event;
      derez.deserialize(done_event);
      
      if (ready.exists() && !ready.has_triggered())
        ready.wait();
#ifdef DEBUG_LEGION
      assert(view->is_instance_view());
#endif
      InstanceView *inst_view = view->as_instance_view();
      inst_view->process_replication_response(done_event, derez);
      Runtime::trigger_event(done_event);
    }

    //--------------------------------------------------------------------------
    /*static*/ void InstanceView::handle_view_replication_removal(
                   Deserializer &derez, Runtime *runtime, AddressSpaceID source)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez);
      DistributedID did;
      derez.deserialize(did);
      RtEvent ready = RtEvent::NO_RT_EVENT;
      LogicalView *view = runtime->find_or_request_logical_view(did, ready);

      FieldMask removal_mask;
      derez.deserialize(removal_mask);
      RtUserEvent done_event;
      derez.deserialize(done_event);
      
      if (ready.exists() && !ready.has_triggered())
        ready.wait();
#ifdef DEBUG_LEGION
      assert(view->is_instance_view());
#endif
      InstanceView *inst_view = view->as_instance_view();
      inst_view->process_replication_removal(source, removal_mask);
      // Trigger the done event now that we are done
      Runtime::trigger_event(done_event);
    }
#endif // ENABLE_VIEW_REPLICATION

    /////////////////////////////////////////////////////////////
    // CollectableView 
    /////////////////////////////////////////////////////////////

    //--------------------------------------------------------------------------
    void CollectableView::defer_collect_user(PhysicalManager *manager,
                 ApEvent term_event, RtEvent collect, ReferenceMutator *mutator)
    //--------------------------------------------------------------------------
    {
      // The runtime will add the gc reference to this view when necessary
      std::set<ApEvent> to_collect;
      bool add_ref = false;
      bool remove_ref = false;
      manager->defer_collect_user(this, term_event, collect,
                                  to_collect, add_ref, remove_ref);
      if (add_ref)
        add_collectable_reference(mutator);
      if (!to_collect.empty())
        collect_users(to_collect); 
      if (remove_ref && remove_collectable_reference(mutator))
        delete this;
    }

    //--------------------------------------------------------------------------
    /*static*/ void CollectableView::handle_deferred_collect(
                                            CollectableView *view, 
                                            const std::set<ApEvent> &to_collect)
    //--------------------------------------------------------------------------
    {
      view->collect_users(to_collect);
      // Then remove the gc reference on the object
      if (view->remove_collectable_reference(NULL))
        delete view;
    }

    /////////////////////////////////////////////////////////////
    // ExprView
    /////////////////////////////////////////////////////////////

    //--------------------------------------------------------------------------
    ExprView::ExprView(RegionTreeForest *ctx, PhysicalManager *man, 
                       MaterializedView *view, IndexSpaceExpression *exp) 
      : context(ctx), manager(man), inst_view(view),
        view_expr(exp), view_volume(SIZE_MAX),
#if defined(DEBUG_LEGION_GC) || defined(LEGION_GC)
        view_did(view->did),
#endif
        invalid_fields(FieldMask(LEGION_FIELD_MASK_FIELD_ALL_ONES))
    //--------------------------------------------------------------------------
    {
      view_expr->add_nested_expression_reference(view->did);
    }

    //--------------------------------------------------------------------------
    ExprView::~ExprView(void)
    //--------------------------------------------------------------------------
    {
#if defined(DEBUG_LEGION_GC) || defined(LEGION_GC)
      if (view_expr->remove_nested_expression_reference(view_did))
        delete view_expr;
#else
      // We can lie about the did here since its not actually used
      if (view_expr->remove_nested_expression_reference(0/*bogus did*/))
        delete view_expr;
#endif
      if (!subviews.empty())
      {
        for (FieldMaskSet<ExprView>::iterator it = subviews.begin();
              it != subviews.end(); it++)
          if (it->first->remove_reference())
            delete it->first;
      }
      // If we have any current or previous users filter them out now
      if (!current_epoch_users.empty())
      {
        for (EventFieldUsers::const_iterator eit = current_epoch_users.begin();
              eit != current_epoch_users.end(); eit++)
        {
          for (FieldMaskSet<PhysicalUser>::const_iterator it = 
                eit->second.begin(); it != eit->second.end(); it++)
            if (it->first->remove_reference())
              delete it->first;
        }
        current_epoch_users.clear();
      }
      if (!previous_epoch_users.empty())
      {
        for (EventFieldUsers::const_iterator eit = previous_epoch_users.begin();
              eit != previous_epoch_users.end(); eit++)
        {
          for (FieldMaskSet<PhysicalUser>::const_iterator it = 
                eit->second.begin(); it != eit->second.end(); it++)
            if (it->first->remove_reference())
              delete it->first;
        }
        previous_epoch_users.clear();
      }
    }

    //--------------------------------------------------------------------------
    size_t ExprView::get_view_volume(void)
    //--------------------------------------------------------------------------
    {
      size_t result = view_volume.load();
      if (result != SIZE_MAX)
        return result;
      result = view_expr->get_volume();
#ifdef DEBUG_LEGION
      assert(result != SIZE_MAX);
#endif
      view_volume.store(result);
      return result;
    }

    //--------------------------------------------------------------------------
    /*static*/ void ExprView::verify_current_to_filter(
                 const FieldMask &dominated, EventFieldUsers &current_to_filter)
    //--------------------------------------------------------------------------
    {
      if (!!dominated)
      {
        for (EventFieldUsers::iterator eit = current_to_filter.begin();
              eit != current_to_filter.end(); /*nothing*/)
        {
          const FieldMask non_dominated = 
            eit->second.get_valid_mask() - dominated;
          // If everything was actually dominated we can keep going
          if (!non_dominated)
          {
            eit++;
            continue;
          }
          // If no fields were dominated we can just remove this
          if (non_dominated == eit->second.get_valid_mask())
          {
            EventFieldUsers::iterator to_delete = eit++;
            current_to_filter.erase(to_delete);
            continue;
          }
          // Otherwise do the actuall overlapping test
          std::vector<PhysicalUser*> to_delete; 
          for (FieldMaskSet<PhysicalUser>::iterator it =
                eit->second.begin(); it != eit->second.end(); it++)
          {
            it.filter(non_dominated);
            if (!it->second)
              to_delete.push_back(it->first);
          }
          if (!eit->second.tighten_valid_mask())
          {
            EventFieldUsers::iterator to_delete = eit++;
            current_to_filter.erase(to_delete);
          }
          else
          {
            for (std::vector<PhysicalUser*>::const_iterator it = 
                  to_delete.begin(); it != to_delete.end(); it++)
              eit->second.erase(*it);
            eit++;
          }
        }
      }
      else
        current_to_filter.clear();
    } 

    //--------------------------------------------------------------------------
    void ExprView::find_user_preconditions(const RegionUsage &usage,
                                           IndexSpaceExpression *user_expr,
                                           const bool user_dominates,
                                           const FieldMask &user_mask,
                                           ApEvent term_event,
                                           UniqueID op_id, unsigned index,
                                           std::set<ApEvent> &preconditions,
                                           const bool trace_recording)
    //--------------------------------------------------------------------------
    {
      DETAILED_PROFILER(Internal::implicit_runtime, 
                        MATERIALIZED_VIEW_FIND_LOCAL_PRECONDITIONS_CALL);
      FieldMask dominated;
      std::set<ApEvent> dead_events; 
      EventFieldUsers current_to_filter, previous_to_filter;
      // Perform the analysis with a read-only lock
      {
        AutoLock v_lock(view_lock,1,false/*exclusive*/);
        // Check to see if we dominate when doing this analysis and
        // can therefore filter or whether we are just intersecting
        // Do the local analysis
        if (user_dominates)
        {
          // We dominate in this case so we can do filtering
          if (!current_epoch_users.empty())
          {
            FieldMask observed, non_dominated;
            find_current_preconditions(usage, user_mask, user_expr,
                                       term_event, op_id, index, 
                                       user_dominates, preconditions, 
                                       dead_events, current_to_filter, 
                                       observed, non_dominated,trace_recording);
            if (!!observed)
              dominated = observed - non_dominated;
          }
          if (!previous_epoch_users.empty())
          {
            if (!!dominated)
              find_previous_filter_users(dominated, previous_to_filter);
            const FieldMask previous_mask = user_mask - dominated;
            if (!!previous_mask)
              find_previous_preconditions(usage, previous_mask, user_expr,
                                          term_event, op_id, index,
                                          user_dominates, preconditions,
                                          dead_events, trace_recording);
          }
        }
        else
        {
          if (!current_epoch_users.empty())
          {
            FieldMask observed, non_dominated;
            find_current_preconditions(usage, user_mask, user_expr,
                                       term_event, op_id, index, 
                                       user_dominates, preconditions, 
                                       dead_events, current_to_filter, 
                                       observed, non_dominated,trace_recording);
#ifdef DEBUG_LEGION
            assert(!observed);
            assert(current_to_filter.empty());
#endif
          }
          if (!previous_epoch_users.empty())
            find_previous_preconditions(usage, user_mask, user_expr,
                                        term_event, op_id, index,
                                        user_dominates, preconditions,
                                        dead_events, trace_recording);
        }
      } 
      // It's possible that we recorded some users for fields which
      // are not actually fully dominated, if so we need to prune them
      // otherwise we can get into issues of soundness
      if (!current_to_filter.empty())
        verify_current_to_filter(dominated, current_to_filter);
      if (!trace_recording && (!dead_events.empty() || 
           !previous_to_filter.empty() || !current_to_filter.empty()))
      {
        // Need exclusive permissions to modify data structures
        AutoLock v_lock(view_lock);
        if (!dead_events.empty())
          for (std::set<ApEvent>::const_iterator it = dead_events.begin();
                it != dead_events.end(); it++)
            filter_local_users(*it); 
        if (!previous_to_filter.empty())
          filter_previous_users(previous_to_filter);
        if (!current_to_filter.empty())
          filter_current_users(current_to_filter);
      }
      // Then see if there are any users below that we need to traverse
      if (!subviews.empty() && 
          !(subviews.get_valid_mask() * user_mask))
      {
        FieldMaskSet<ExprView> to_traverse;
        std::map<ExprView*,IndexSpaceExpression*> traverse_exprs;
        for (FieldMaskSet<ExprView>::const_iterator it = 
              subviews.begin(); it != subviews.end(); it++)
        {
          FieldMask overlap = it->second & user_mask;
          if (!overlap)
            continue;
          // If we've already determined the user dominates
          // then we don't even have to do this test
          if (user_dominates)
          {
            to_traverse.insert(it->first, overlap);
            continue;
          }
          if (it->first->view_expr == user_expr)
          {
            to_traverse.insert(it->first, overlap);
            traverse_exprs[it->first] = user_expr;
            continue;
          }
          IndexSpaceExpression *expr_overlap = 
            context->intersect_index_spaces(it->first->view_expr, user_expr);
          if (!expr_overlap->is_empty())
          {
            to_traverse.insert(it->first, overlap);
            traverse_exprs[it->first] = expr_overlap;
          }
        }
        if (!to_traverse.empty())
        {
          if (user_dominates)
          {
            for (FieldMaskSet<ExprView>::const_iterator it = 
                  to_traverse.begin(); it != to_traverse.end(); it++)
              it->first->find_user_preconditions(usage, it->first->view_expr,
                                    true/*dominate*/, it->second, term_event,
                                    op_id, index,preconditions,trace_recording);
          }
          else
          {
            for (FieldMaskSet<ExprView>::const_iterator it = 
                  to_traverse.begin(); it != to_traverse.end(); it++)
            {
              IndexSpaceExpression *intersect = traverse_exprs[it->first];
              const bool user_dominates = 
                (intersect->expr_id == it->first->view_expr->expr_id) ||
                (intersect->get_volume() == it->first->get_view_volume());
              it->first->find_user_preconditions(usage, intersect, 
                            user_dominates, it->second, term_event, 
                            op_id, index, preconditions, trace_recording);
            }
          }
        }
      }
    }

    //--------------------------------------------------------------------------
    void ExprView::find_copy_preconditions(const RegionUsage &usage,
                                           IndexSpaceExpression *copy_expr,
                                           const bool copy_dominates,
                                           const FieldMask &copy_mask,
                                           UniqueID op_id, unsigned index,
                                           std::set<ApEvent> &preconditions,
                                           const bool trace_recording)
    //--------------------------------------------------------------------------
    {
      DETAILED_PROFILER(Internal::implicit_runtime, 
                        MATERIALIZED_VIEW_FIND_LOCAL_COPY_PRECONDITIONS_CALL);
      FieldMask dominated;
      std::set<ApEvent> dead_events; 
      EventFieldUsers current_to_filter, previous_to_filter;
      // Do the first pass with a read-only lock on the events
      {
        AutoLock v_lock(view_lock,1,false/*exclusive*/);
        // Check to see if we dominate when doing this analysis and
        // can therefore filter or whether we are just intersecting
        // Do the local analysis
        if (copy_dominates)
        {
          // We dominate in this case so we can do filtering
          if (!current_epoch_users.empty())
          {
            FieldMask observed, non_dominated;
            find_current_preconditions(usage, copy_mask, copy_expr, 
                                       op_id, index, copy_dominates,
                                       preconditions, dead_events, 
                                       current_to_filter, observed, 
                                       non_dominated, trace_recording);
            if (!!observed)
              dominated = observed - non_dominated;
          }
          if (!previous_epoch_users.empty())
          {
            if (!!dominated)
              find_previous_filter_users(dominated, previous_to_filter);
            const FieldMask previous_mask = copy_mask - dominated;
            if (!!previous_mask)
              find_previous_preconditions(usage, previous_mask,
                                          copy_expr, op_id, index,
                                          copy_dominates, preconditions,
                                          dead_events, trace_recording);
          }
        }
        else
        {
          if (!current_epoch_users.empty())
          {
            FieldMask observed, non_dominated;
            find_current_preconditions(usage, copy_mask, copy_expr,
                                       op_id, index, copy_dominates,
                                       preconditions, dead_events, 
                                       current_to_filter, observed, 
                                       non_dominated, trace_recording);
#ifdef DEBUG_LEGION
            assert(!observed);
            assert(current_to_filter.empty());
#endif
          }
          if (!previous_epoch_users.empty())
            find_previous_preconditions(usage, copy_mask, copy_expr,
                                        op_id, index, copy_dominates,
                                        preconditions, dead_events,
                                        trace_recording);
        }
      }
      // It's possible that we recorded some users for fields which
      // are not actually fully dominated, if so we need to prune them
      // otherwise we can get into issues of soundness
      if (!current_to_filter.empty())
        verify_current_to_filter(dominated, current_to_filter);
      if (!trace_recording && (!dead_events.empty() || 
           !previous_to_filter.empty() || !current_to_filter.empty()))
      {
        // Need exclusive permissions to modify data structures
        AutoLock v_lock(view_lock);
        if (!dead_events.empty())
          for (std::set<ApEvent>::const_iterator it = dead_events.begin();
                it != dead_events.end(); it++)
            filter_local_users(*it); 
        if (!previous_to_filter.empty())
          filter_previous_users(previous_to_filter);
        if (!current_to_filter.empty())
          filter_current_users(current_to_filter);
      }
      // Then see if there are any users below that we need to traverse
      if (!subviews.empty() && 
          !(subviews.get_valid_mask() * copy_mask))
      {
        for (FieldMaskSet<ExprView>::const_iterator it = 
              subviews.begin(); it != subviews.end(); it++)
        {
          FieldMask overlap = it->second & copy_mask;
          if (!overlap)
            continue;
          // If the copy dominates then we don't even have
          // to do the intersection test
          if (copy_dominates)
          {
            it->first->find_copy_preconditions(usage, it->first->view_expr,
                                    true/*dominate*/, overlap, op_id, index,
                                    preconditions, trace_recording);
            continue;
          }
          if (it->first->view_expr == copy_expr)
          {
            it->first->find_copy_preconditions(usage, copy_expr,
                                    true/*dominate*/, overlap, op_id, index,
                                    preconditions, trace_recording);
            continue;
          }
          IndexSpaceExpression *expr_overlap = 
            context->intersect_index_spaces(it->first->view_expr, copy_expr);
          if (!expr_overlap->is_empty())
          {
            const bool copy_dominates = 
              (expr_overlap->expr_id == it->first->view_expr->expr_id) ||
              (expr_overlap->get_volume() == it->first->get_view_volume());
            it->first->find_copy_preconditions(usage, expr_overlap, 
                              copy_dominates, overlap, op_id, 
                              index, preconditions, trace_recording);
          }
        }
      }
    }

    //--------------------------------------------------------------------------
    void ExprView::find_last_users(const RegionUsage &usage,
                                   IndexSpaceExpression *expr,
                                   const bool expr_dominates,
                                   const FieldMask &mask,
                                   std::set<ApEvent> &last_events) const
    //--------------------------------------------------------------------------
    {
      // See if there are any users below that we need to traverse
      if (!subviews.empty() && !(subviews.get_valid_mask() * mask))
      {
        for (FieldMaskSet<ExprView>::const_iterator it = 
              subviews.begin(); it != subviews.end(); it++)
        {
          FieldMask overlap = it->second & mask;
          if (!overlap)
            continue;
          // If the expr dominates then we don't even have
          // to do the intersection test
          if (expr_dominates)
          {
            it->first->find_last_users(usage, it->first->view_expr,
                            true/*dominate*/, overlap, last_events);
            continue;
          }
          if (it->first->view_expr == expr)
          {
            it->first->find_last_users(usage, expr,
                true/*dominate*/, overlap, last_events);
            continue;
          }
          IndexSpaceExpression *expr_overlap = 
            context->intersect_index_spaces(it->first->view_expr, expr);
          if (!expr_overlap->is_empty())
          {
            const bool dominates = 
              (expr_overlap->expr_id == it->first->view_expr->expr_id) ||
              (expr_overlap->get_volume() == it->first->get_view_volume());
            it->first->find_last_users(usage, expr_overlap,
                          dominates, overlap, last_events); 
          }
        }
      }
      FieldMask dominated;
      // Now we can traverse at this level
      AutoLock v_lock(view_lock,1,false/*exclusive*/);
      // We dominate in this case so we can do filtering
      if (!current_epoch_users.empty())
      {
        FieldMask observed, non_dominated;
        find_current_preconditions(usage, mask, expr, 
                                   expr_dominates, last_events,
                                   observed, non_dominated);
        if (!!observed)
          dominated = observed - non_dominated;
      }
      if (!previous_epoch_users.empty())
      {
        const FieldMask previous_mask = mask - dominated;
        if (!!previous_mask)
          find_previous_preconditions(usage, previous_mask,
                                      expr, expr_dominates, last_events);
      }
    }

    //--------------------------------------------------------------------------
    ExprView* ExprView::find_congruent_view(IndexSpaceExpression *expr)
    //--------------------------------------------------------------------------
    {
      // Handle the base case first
      if ((expr == view_expr) || (expr->get_volume() == get_view_volume()))
        return const_cast<ExprView*>(this);
      for (FieldMaskSet<ExprView>::const_iterator it = 
            subviews.begin(); it != subviews.end(); it++)
      {
        if (it->first->view_expr == expr)
          return it->first;
        IndexSpaceExpression *overlap =
          context->intersect_index_spaces(expr, it->first->view_expr);
        const size_t overlap_volume = overlap->get_volume();
        if (overlap_volume == 0)
          continue;
        // See if we dominate or just intersect
        if (overlap_volume == expr->get_volume())
        {
          // See if we strictly dominate or whether they are equal
          if (overlap_volume < it->first->get_view_volume())
          {
            ExprView *result = it->first->find_congruent_view(expr);
            if (result != NULL)
              return result;
          }
          else // Otherwise we're the same 
            return it->first;
        }
      }
      return NULL;
    }

    //--------------------------------------------------------------------------
    void ExprView::insert_subview(ExprView *subview, FieldMask &subview_mask)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(this != subview);
#endif
      // Iterate over all subviews and see which ones we dominate and which
      // ones dominate the subview
      if (!subviews.empty() && !(subviews.get_valid_mask() * subview_mask))
      {
        bool need_tighten = true;
        std::vector<ExprView*> to_delete;
        FieldMaskSet<ExprView> dominating_subviews;
        for (FieldMaskSet<ExprView>::iterator it = 
              subviews.begin(); it != subviews.end(); it++)
        {
          // See if we intersect on fields
          FieldMask overlap_mask = it->second & subview_mask;
          if (!overlap_mask)
            continue;
          IndexSpaceExpression *overlap =
            context->intersect_index_spaces(subview->view_expr,
                                            it->first->view_expr);
          const size_t overlap_volume = overlap->get_volume();
          if (overlap_volume == 0)
            continue;
          // See if we dominate or just intersect
          if (overlap_volume == subview->get_view_volume())
          {
#ifdef DEBUG_LEGION
            // Should only strictly dominate if they were congruent
            // then we wouldn't be inserting in the first place
            assert(overlap_volume < it->first->get_view_volume());
#endif
            // Dominator so we can just continue traversing
            dominating_subviews.insert(it->first, overlap_mask);
          }
          else if (overlap_volume == it->first->get_view_volume())
          {
#ifdef DEBUG_LEGION
            assert(overlap_mask * dominating_subviews.get_valid_mask());
#endif
            // We dominate this view so we can just pull it 
            // in underneath of us now
            it.filter(overlap_mask);
            subview->insert_subview(it->first, overlap_mask);
            need_tighten = true;
            // See if we need to remove this subview
            if (!it->second)
              to_delete.push_back(it->first);
          }
          // Otherwise it's just a normal intersection
        }
        // See if we had any dominators
        if (!dominating_subviews.empty())
        {
          if (dominating_subviews.size() > 1)
          {
            // We need to deduplicate finding or making the new ExprView
            // First check to see if we have it already in one sub-tree
            // If not, we'll pick the one with the smallest bounding volume
            LegionMap<std::pair<size_t/*volume*/,ExprView*>,FieldMask>
              sorted_subviews;
            for (FieldMaskSet<ExprView>::const_iterator it = 
                  dominating_subviews.begin(); it != 
                  dominating_subviews.end(); it++)
            {
              FieldMask overlap = it->second;
              // Channeling Tuco here
              it->first->find_tightest_subviews(subview->view_expr, overlap,
                                                sorted_subviews);
            }
            for (LegionMap<std::pair<size_t,ExprView*>,FieldMask>::
                  const_iterator it = sorted_subviews.begin(); it !=
                  sorted_subviews.end(); it++)
            {
              FieldMask overlap = it->second & subview_mask;
              if (!overlap)
                continue;
              subview_mask -= overlap;
              it->first.second->insert_subview(subview, overlap);
              if (!subview_mask || 
                  (subview_mask * dominating_subviews.get_valid_mask()))
                break;
            }
#ifdef DEBUG_LEGION
            assert(subview_mask * dominating_subviews.get_valid_mask());
#endif
          }
          else
          {
            FieldMaskSet<ExprView>::const_iterator first = 
              dominating_subviews.begin();
            FieldMask dominated_mask = first->second; 
            subview_mask -= dominated_mask;
            first->first->insert_subview(subview, dominated_mask);
          }
        }
        if (!to_delete.empty())
        {
          for (std::vector<ExprView*>::const_iterator it = 
                to_delete.begin(); it != to_delete.end(); it++)
          {
            subviews.erase(*it);
            if ((*it)->remove_reference())
              delete (*it);
          }
        }
        if (need_tighten)
          subviews.tighten_valid_mask();
      }
      // If we make it here and there are still fields then we need to 
      // add it locally
      if (!!subview_mask && subviews.insert(subview, subview_mask))
        subview->add_reference();
    }

    //--------------------------------------------------------------------------
    void ExprView::find_tightest_subviews(IndexSpaceExpression *expr,
                                          FieldMask &expr_mask,
                                          LegionMap<std::pair<size_t,ExprView*>,
                                                     FieldMask> &bounding_views)
    //--------------------------------------------------------------------------
    {
      if (!subviews.empty() && !(expr_mask * subviews.get_valid_mask()))
      {
        FieldMask dominated_mask;
        for (FieldMaskSet<ExprView>::iterator it = subviews.begin();
              it != subviews.end(); it++)
        {
          // See if we intersect on fields
          FieldMask overlap_mask = it->second & expr_mask;
          if (!overlap_mask)
            continue;
          IndexSpaceExpression *overlap =
            context->intersect_index_spaces(expr, it->first->view_expr);
          const size_t overlap_volume = overlap->get_volume();
          if (overlap_volume == 0)
            continue;
          // See if we dominate or just intersect
          if (overlap_volume == expr->get_volume())
          {
#ifdef DEBUG_LEGION
            // Should strictly dominate otherwise we'd be congruent
            assert(overlap_volume < it->first->get_view_volume());
#endif
            dominated_mask |= overlap_mask;
            // Continute the traversal
            it->first->find_tightest_subviews(expr,overlap_mask,bounding_views);
          }
        }
        // Remove any dominated fields from below
        if (!!dominated_mask)
          expr_mask -= dominated_mask;
      }
      // If we still have fields then record ourself
      if (!!expr_mask)
      {
        std::pair<size_t,ExprView*> key(get_view_volume(), this);
        bounding_views[key] |= expr_mask;
      }
    }

    //--------------------------------------------------------------------------
    void ExprView::add_partial_user(const RegionUsage &usage,
                                    UniqueID op_id, unsigned index,
                                    FieldMask user_mask,
                                    const ApEvent term_event,
                                    const RtEvent collect_event,
                                    IndexSpaceExpression *user_expr,
                                    const size_t user_volume,
                                    const bool trace_recording)
    //--------------------------------------------------------------------------
    {
      // We're going to try to put this user as far down the ExprView tree
      // as we can in order to avoid doing unnecessary intersection tests later
      {
        // Find all the intersecting subviews to see if we can 
        // continue the traversal
        // No need for the view lock anymore since we're protected
        // by the expr_lock at the top of the tree
        //AutoLock v_lock(view_lock,1,false/*exclusive*/); 
        for (FieldMaskSet<ExprView>::const_iterator it = 
              subviews.begin(); it != subviews.end(); it++)
        {
          // If the fields don't overlap then we don't care
          const FieldMask overlap_mask = it->second & user_mask;
          if (!overlap_mask)
            continue;
          IndexSpaceExpression *overlap =
            context->intersect_index_spaces(user_expr, it->first->view_expr);
          const size_t overlap_volume = overlap->get_volume();
          if (overlap_volume == user_volume)
          {
            // Check for the cases where we dominated perfectly
            if (overlap_volume == it->first->view_volume)
            {
#ifdef ENABLE_VIEW_REPLICATION
              PhysicalUser *dominate_user = new PhysicalUser(usage,
                  it->first->view_expr, op_id, index, collect_event,
                  true/*copy*/, true/*covers*/);
#else
              PhysicalUser *dominate_user = new PhysicalUser(usage,
                  it->first->view_expr,op_id,index,true/*copy*/,true/*covers*/);
#endif
              it->first->add_current_user(dominate_user, term_event, 
                      collect_event, overlap_mask, trace_recording);
            }
            else
            {
              // Continue the traversal on this node
              it->first->add_partial_user(usage, op_id, index, overlap_mask,
                                          term_event, collect_event, user_expr,
                                          user_volume, trace_recording);
            }
            // We only need to record the partial user in one sub-tree
            // where it is dominated in order to be sound
            user_mask -= overlap_mask;
            if (!user_mask)
              break;
          }
          // Otherwise for all other cases we're going to record it here
          // because they don't dominate the user to be recorded
        }
      }
      // If we still have local fields, make a user and record it here
      if (!!user_mask)
      {
#ifdef ENABLE_VIEW_REPLICATION
        PhysicalUser *user = new PhysicalUser(usage, user_expr, op_id, index,
                                collect_event, true/*copy*/, false/*covers*/);
#else
        PhysicalUser *user = new PhysicalUser(usage, user_expr, op_id, index,
                                              true/*copy*/, false/*covers*/);
#endif
        add_current_user(user, term_event, collect_event, 
                         user_mask, trace_recording);
      }
    }

    //--------------------------------------------------------------------------
    void ExprView::add_current_user(PhysicalUser *user,const ApEvent term_event,
                              RtEvent collect_event, const FieldMask &user_mask,
                              const bool trace_recording)
    //--------------------------------------------------------------------------
    {
      bool issue_collect = true;
      {
        AutoLock v_lock(view_lock);
        EventUsers &event_users = current_epoch_users[term_event];
        if (event_users.insert(user, user_mask))
          user->add_reference();
        else
          issue_collect = false;
      }
      if (issue_collect)
        defer_collect_user(manager, term_event, collect_event);
    }

    //--------------------------------------------------------------------------
    void ExprView::clean_views(FieldMask &valid_mask, 
                               FieldMaskSet<ExprView> &clean_set)
    //--------------------------------------------------------------------------
    {
      // Handle the base case if we already did it
      FieldMaskSet<ExprView>::const_iterator finder = clean_set.find(this);
      if (finder != clean_set.end())
      {
        valid_mask = finder->second;
        return;
      }
      // No need to hold the lock for this part we know that no one
      // is going to be modifying this data structure at the same time
      FieldMaskSet<ExprView> new_subviews;
      std::vector<ExprView*> to_delete;
      for (FieldMaskSet<ExprView>::iterator it = subviews.begin();
            it != subviews.end(); it++)
      {
        FieldMask new_mask;
        it->first->clean_views(new_mask, clean_set);
        // Save this as part of the valid mask without filtering
        valid_mask |= new_mask;
        // Have to make sure to filter this by the previous set of fields 
        // since we could get more than we initially had
        // We also need update the invalid fields if we remove a path
        // to the subview
        if (!!new_mask)
        {
          new_mask &= it->second;
          const FieldMask new_invalid = it->second - new_mask;
          if (!!new_invalid)
          {
#ifdef DEBUG_LEGION
            // Should only have been one path here
            assert(it->first->invalid_fields * new_invalid);
#endif
            it->first->invalid_fields |= new_invalid;
          }
        }
        else
        {
#ifdef DEBUG_LEGION
          // Should only have been one path here
          assert(it->first->invalid_fields * it->second);
#endif
          it->first->invalid_fields |= it->second;
        }
        if (!!new_mask)
          new_subviews.insert(it->first, new_mask);
        else
          to_delete.push_back(it->first);
      }
      subviews.swap(new_subviews);
      if (!to_delete.empty())
      {
        for (std::vector<ExprView*>::const_iterator it = 
              to_delete.begin(); it != to_delete.end(); it++)
          if ((*it)->remove_reference())
            delete (*it);
      }
      AutoLock v_lock(view_lock);
      if (!current_epoch_users.empty())
      {
        for (EventFieldUsers::const_iterator it = 
              current_epoch_users.begin(); it != 
              current_epoch_users.end(); it++)
          valid_mask |= it->second.get_valid_mask();
      }
      if (!previous_epoch_users.empty())
      {
        for (EventFieldUsers::const_iterator it = 
              previous_epoch_users.begin(); it != 
              previous_epoch_users.end(); it++)
          valid_mask |= it->second.get_valid_mask();
      }
      // Save this for the future so we don't need to compute it again
      if (clean_set.insert(this, valid_mask))
        add_reference();
    }

    //--------------------------------------------------------------------------
    void ExprView::pack_replication(Serializer &rez,
                                    std::map<PhysicalUser*,unsigned> &indexes,
                                    const FieldMask &pack_mask,
                                    const AddressSpaceID target) const
    //--------------------------------------------------------------------------
    {
      RezCheck z(rez);
      {
        // Need a read-only lock here to protect against garbage collection
        // tasks coming back through and pruning out current epoch users
        // but we know there are no other modifications happening in parallel
        // because the replicated lock at the top prevents any new users
        // from being added while we're doing this pack
        AutoLock v_lock(view_lock,1,false/*exclusive*/);
        // Pack the current users
        EventFieldUsers needed_current; 
        for (EventFieldUsers::const_iterator eit = current_epoch_users.begin();
              eit != current_epoch_users.end(); eit++)
        {
          if (eit->second.get_valid_mask() * pack_mask)
            continue;
          FieldMaskSet<PhysicalUser> &needed = needed_current[eit->first];
          for (FieldMaskSet<PhysicalUser>::const_iterator it = 
                eit->second.begin(); it != eit->second.end(); it++)
          {
            const FieldMask overlap = it->second & pack_mask;
            if (!overlap)
              continue;
            needed.insert(it->first, overlap);
          }
        }
        rez.serialize<size_t>(needed_current.size());
        for (EventFieldUsers::const_iterator eit = needed_current.begin();
              eit != needed_current.end(); eit++)
        {
          rez.serialize(eit->first);
          rez.serialize<size_t>(eit->second.size());
          for (FieldMaskSet<PhysicalUser>::const_iterator it = 
                eit->second.begin(); it != eit->second.end(); it++)
          {
            // See if we already packed this before or not
            std::map<PhysicalUser*,unsigned>::const_iterator finder = 
              indexes.find(it->first);
            if (finder == indexes.end())
            {
              const unsigned index = indexes.size();
              rez.serialize(index);
              it->first->pack_user(rez, target);
              indexes[it->first] = index;
            }
            else
              rez.serialize(finder->second);
            rez.serialize(it->second);
          }
        }
        // Pack the previous users
        EventFieldUsers needed_previous; 
        for (EventFieldUsers::const_iterator eit = previous_epoch_users.begin();
              eit != previous_epoch_users.end(); eit++)
        {
          if (eit->second.get_valid_mask() * pack_mask)
            continue;
          FieldMaskSet<PhysicalUser> &needed = needed_previous[eit->first];
          for (FieldMaskSet<PhysicalUser>::const_iterator it = 
                eit->second.begin(); it != eit->second.end(); it++)
          {
            const FieldMask overlap = it->second & pack_mask;
            if (!overlap)
              continue;
            needed.insert(it->first, overlap);
          }
        }
        rez.serialize<size_t>(needed_previous.size());
        for (EventFieldUsers::const_iterator eit = needed_previous.begin();
              eit != needed_previous.end(); eit++)
        {
          rez.serialize(eit->first);
          rez.serialize<size_t>(eit->second.size());
          for (FieldMaskSet<PhysicalUser>::const_iterator it = 
                eit->second.begin(); it != eit->second.end(); it++)
          {
            // See if we already packed this before or not
            std::map<PhysicalUser*,unsigned>::const_iterator finder = 
              indexes.find(it->first);
            if (finder == indexes.end())
            {
              const unsigned index = indexes.size();
              rez.serialize(index);
              it->first->pack_user(rez, target);
              indexes[it->first] = index;
            }
            else
              rez.serialize(finder->second);
            rez.serialize(it->second);
          }
        }
      }
      // Pack the needed subviews no need for a lock here
      // since we know that we're protected by the expr_lock
      // at the top of the tree
      FieldMaskSet<ExprView> needed_subviews;
      for (FieldMaskSet<ExprView>::const_iterator it = 
            subviews.begin(); it != subviews.end(); it++)
      {
        const FieldMask overlap = it->second & pack_mask;
        if (!overlap)
          continue;
        needed_subviews.insert(it->first, overlap);
      }
      rez.serialize<size_t>(needed_subviews.size());
      for (FieldMaskSet<ExprView>::const_iterator it = 
            needed_subviews.begin(); it != needed_subviews.end(); it++)
      {
        it->first->view_expr->pack_expression(rez, target);
        rez.serialize(it->second);
        it->first->pack_replication(rez, indexes, it->second, target);
      }
    }
    
    //--------------------------------------------------------------------------
    void ExprView::unpack_replication(Deserializer &derez, ExprView *root,
                              const AddressSpaceID source,
                              std::map<IndexSpaceExprID,ExprView*> &expr_cache,
                              std::vector<PhysicalUser*> &users)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez);
      std::map<ApEvent,RtEvent> to_collect;
      // Need a read-write lock since we're going to be mutating the structures
      {
        AutoLock v_lock(view_lock);
        size_t num_current;
        derez.deserialize(num_current);
        for (unsigned idx1 = 0; idx1 < num_current; idx1++)
        {
          ApEvent user_event;
          derez.deserialize(user_event);
          FieldMaskSet<PhysicalUser> &current_users = 
            current_epoch_users[user_event];
#ifndef ENABLE_VIEW_REPLICATION
          if (current_users.empty())
            to_collect[user_event] = RtEvent::NO_RT_EVENT;
#endif
          size_t num_users;
          derez.deserialize(num_users);
          for (unsigned idx2 = 0; idx2 < num_users; idx2++)
          {
            unsigned user_index;
            derez.deserialize(user_index);
            if (user_index >= users.size())
            {
#ifdef DEBUG_LEGION
              assert(user_index == users.size());
#endif
              users.push_back(PhysicalUser::unpack_user(derez, context,source));
              // Add a reference to prevent this being deleted
              // before we're done unpacking
              users.back()->add_reference();
#ifdef ENABLE_VIEW_REPLICATION
              to_collect[user_event] = users.back()->collect_event;
#endif
            }
            FieldMask user_mask;
            derez.deserialize(user_mask);
            if (current_users.insert(users[user_index], user_mask))
              users[user_index]->add_reference();
          }
        }
        size_t num_previous;
        derez.deserialize(num_previous);
        for (unsigned idx1 = 0; idx1 < num_previous; idx1++)
        {
          ApEvent user_event;
          derez.deserialize(user_event);
          FieldMaskSet<PhysicalUser> &previous_users = 
            previous_epoch_users[user_event];
#ifndef ENABLE_VIEW_REPLICATION
          if (previous_users.empty())
            to_collect[user_event] = RtEvent::NO_RT_EVENT;
#endif
          size_t num_users;
          derez.deserialize(num_users);
          for (unsigned idx2 = 0; idx2 < num_users; idx2++)
          {
            unsigned user_index;
            derez.deserialize(user_index);
            if (user_index >= users.size())
            {
#ifdef DEBUG_LEGION
              assert(user_index == users.size());
#endif
              users.push_back(PhysicalUser::unpack_user(derez, context,source));
              // Add a reference to prevent this being deleted
              // before we're done unpacking
              users.back()->add_reference();
#ifdef ENABLE_VIEW_REPLICATION
              to_collect[user_event] = users.back()->collect_event;
#endif
            }
            FieldMask user_mask;
            derez.deserialize(user_mask);
            if (previous_users.insert(users[user_index], user_mask))
              users[user_index]->add_reference();
          }
        }
      }
      size_t num_subviews;
      derez.deserialize(num_subviews);
      if (num_subviews > 0)
      {
        for (unsigned idx = 0; idx < num_subviews; idx++)
        {
          IndexSpaceExpression *subview_expr = 
            IndexSpaceExpression::unpack_expression(derez, context, source);
          FieldMask subview_mask;
          derez.deserialize(subview_mask);
          // See if we already have it in the cache
          std::map<IndexSpaceExprID,ExprView*>::const_iterator finder = 
            expr_cache.find(subview_expr->expr_id);
          ExprView *subview = NULL;
          if (finder == expr_cache.end())
          {
            // See if we can find this view in the tree before we make it
            subview = root->find_congruent_view(subview_expr);
            // If it's still NULL then we can make it
            if (subview == NULL)
              subview = new ExprView(context, manager, inst_view, subview_expr);
            expr_cache[subview_expr->expr_id] = subview;
          }
          else
            subview = finder->second;
#ifdef DEBUG_LEGION
          assert(subview != NULL);
#endif
          // Check to see if it needs to be inserted
          if (subview != root)
          {
            FieldMask insert_mask = subview->invalid_fields & subview_mask;
            if (!!insert_mask)
            {
              subview->invalid_fields -= insert_mask;
              root->insert_subview(subview, insert_mask);
            }
          }
          // Continue the unpacking
          subview->unpack_replication(derez, root, source, expr_cache, users);
        }
      }
      if (!to_collect.empty())
      {
        for (std::map<ApEvent,RtEvent>::const_iterator it = 
              to_collect.begin(); it != to_collect.end(); it++)
          defer_collect_user(manager, it->first, it->second);
      }
    }

    //--------------------------------------------------------------------------
    void ExprView::deactivate_replication(const FieldMask &deactivate_mask)
    //--------------------------------------------------------------------------
    {
      // Traverse any subviews and do the deactivates in those nodes first
      // No need to get the lock here since we're protected by the 
      // exclusive expr_lock at the top of the tree
      // Don't worry about pruning, when we clean the cache after doing
      // this pass then that will also go through and prune out any 
      // expr views which no longer have users in any subtrees
      for (FieldMaskSet<ExprView>::const_iterator it = 
            subviews.begin(); it != subviews.end(); it++)
      {
        const FieldMask overlap = it->second & deactivate_mask;
        if (!overlap)
          continue;
        it->first->deactivate_replication(overlap);
      }
      // Need a read-write lock since we're going to be mutating the structures
      AutoLock v_lock(view_lock);
      // Prune out the current epoch users
      if (!current_epoch_users.empty())
      {
        std::vector<ApEvent> events_to_delete;
        for (EventFieldUsers::iterator eit = current_epoch_users.begin();
              eit != current_epoch_users.end(); eit++)
        {
          if (eit->second.get_valid_mask() * deactivate_mask)
            continue;
          bool need_tighten = false;
          std::vector<PhysicalUser*> to_delete;
          for (FieldMaskSet<PhysicalUser>::iterator it = 
                eit->second.begin(); it != eit->second.end(); it++)
          {
            if (it->second * deactivate_mask)
              continue;
            need_tighten = true;
            it.filter(deactivate_mask);
            if (!it->second)
              to_delete.push_back(it->first);
          }
          if (!to_delete.empty())
          {
            for (std::vector<PhysicalUser*>::const_iterator it = 
                  to_delete.begin(); it != to_delete.end(); it++)
            {
              eit->second.erase(*it);
              if ((*it)->remove_reference())
                delete (*it);
            }
            if (eit->second.empty())
            {
              events_to_delete.push_back(eit->first);
              continue;
            }
          }
          if (need_tighten)
            eit->second.tighten_valid_mask();
        }
        if (!events_to_delete.empty())
        {
          for (std::vector<ApEvent>::const_iterator it = 
                events_to_delete.begin(); it != events_to_delete.end(); it++)
            current_epoch_users.erase(*it);
        }
      }
      // Prune out the previous epoch users
      if (!previous_epoch_users.empty())
      {
        std::vector<ApEvent> events_to_delete;
        for (EventFieldUsers::iterator eit = previous_epoch_users.begin();
              eit != previous_epoch_users.end(); eit++)
        {
          if (eit->second.get_valid_mask() * deactivate_mask)
            continue;
          bool need_tighten = false;
          std::vector<PhysicalUser*> to_delete;
          for (FieldMaskSet<PhysicalUser>::iterator it = 
                eit->second.begin(); it != eit->second.end(); it++)
          {
            if (it->second * deactivate_mask)
              continue;
            need_tighten = true;
            it.filter(deactivate_mask);
            if (!it->second)
              to_delete.push_back(it->first);
          }
          if (!to_delete.empty())
          {
            for (std::vector<PhysicalUser*>::const_iterator it = 
                  to_delete.begin(); it != to_delete.end(); it++)
            {
              eit->second.erase(*it);
              if ((*it)->remove_reference())
                delete (*it);
            }
            if (eit->second.empty())
            {
              events_to_delete.push_back(eit->first);
              continue;
            }
          }
          if (need_tighten)
            eit->second.tighten_valid_mask();
        }
        if (!events_to_delete.empty())
        {
          for (std::vector<ApEvent>::const_iterator it = 
                events_to_delete.begin(); it != events_to_delete.end(); it++)
            previous_epoch_users.erase(*it);
        }
      } 
    }

    //--------------------------------------------------------------------------
    void ExprView::add_collectable_reference(ReferenceMutator *mutator)
    //--------------------------------------------------------------------------
    {
      add_reference();
      // Only the logical owner adds the full GC reference as this is where
      // the actual garbage collection algorithm will take place and we know
      // that we have all the valid gc event users
      if (inst_view->is_logical_owner())
        inst_view->add_base_gc_ref(PENDING_GC_REF, mutator);
      else
        inst_view->add_base_resource_ref(PENDING_GC_REF);
    }

    //--------------------------------------------------------------------------
    bool ExprView::remove_collectable_reference(ReferenceMutator *mutator)
    //--------------------------------------------------------------------------
    {
      if (inst_view->is_logical_owner())
      {
        if (inst_view->remove_base_gc_ref(PENDING_GC_REF, mutator))
          delete inst_view;
      }
      else
      {
        if (inst_view->remove_base_resource_ref(PENDING_GC_REF))
          delete inst_view;
      }
      return remove_reference();
    }

    //--------------------------------------------------------------------------
    void ExprView::collect_users(const std::set<ApEvent> &to_collect)
    //--------------------------------------------------------------------------
    {
      AutoLock v_lock(view_lock);
      for (std::set<ApEvent>::const_iterator it = 
            to_collect.begin(); it != to_collect.end(); it++)
        filter_local_users(*it);
    }

    //--------------------------------------------------------------------------
    void ExprView::filter_local_users(ApEvent term_event) 
    //--------------------------------------------------------------------------
    {
      // Caller must be holding the lock
      DETAILED_PROFILER(context->runtime, 
                        MATERIALIZED_VIEW_FILTER_LOCAL_USERS_CALL);
      // Don't do this if we are in Legion Spy since we want to see
      // all of the dependences on an instance
#ifndef LEGION_DISABLE_EVENT_PRUNING
      EventFieldUsers::iterator current_finder = 
        current_epoch_users.find(term_event);
      if (current_finder != current_epoch_users.end())
      {
        for (EventUsers::const_iterator it = current_finder->second.begin();
              it != current_finder->second.end(); it++)
          if (it->first->remove_reference())
            delete it->first;
        current_epoch_users.erase(current_finder);
      }
      LegionMap<ApEvent,EventUsers>::iterator previous_finder = 
        previous_epoch_users.find(term_event);
      if (previous_finder != previous_epoch_users.end())
      {
        for (EventUsers::const_iterator it = previous_finder->second.begin();
              it != previous_finder->second.end(); it++)
          if (it->first->remove_reference())
            delete it->first;
        previous_epoch_users.erase(previous_finder);
      }
#endif
    }

    //--------------------------------------------------------------------------
    void ExprView::filter_current_users(const EventFieldUsers &to_filter)
    //--------------------------------------------------------------------------
    {
      // Lock needs to be held by caller 
      for (EventFieldUsers::const_iterator fit = to_filter.begin();
            fit != to_filter.end(); fit++)
      {
        EventFieldUsers::iterator event_finder = 
          current_epoch_users.find(fit->first);
        // If it's already been pruned out then either it was filtered
        // because it finished or someone else moved it already, either
        // way we don't need to do anything about it
        if (event_finder == current_epoch_users.end())
          continue;
        EventFieldUsers::iterator target_finder = 
          previous_epoch_users.find(fit->first);
        for (EventUsers::const_iterator it = fit->second.begin();
              it != fit->second.end(); it++)
        {
          EventUsers::iterator finder = event_finder->second.find(it->first);
          // Might already have been pruned out again, either way there is
          // nothing for us to do here if it was already moved
          if (finder == event_finder->second.end())
            continue;
          const FieldMask overlap = finder->second & it->second;
          if (!overlap)
            continue;
          finder.filter(overlap);
          bool needs_reference = true;
          if (!finder->second)
          {
            // Have the reference flow back with the user
            needs_reference = false;
            event_finder->second.erase(finder);
          }
          // Now add the user to the previous set
          if (target_finder == previous_epoch_users.end())
          {
            if (needs_reference)
              it->first->add_reference();
            previous_epoch_users[fit->first].insert(it->first, overlap);
            target_finder = previous_epoch_users.find(fit->first);
          }
          else
          {
            if (target_finder->second.insert(it->first, overlap))
            {
              // Added a new user to the previous users
              if (needs_reference)
                it->first->add_reference();
            }
            else
            {
              // Remove any extra references we might be trying to send back
              if (!needs_reference && it->first->remove_reference())
                delete it->first;
            }
          }
        }
        if (event_finder->second.empty())
          current_epoch_users.erase(event_finder);
      }
    }

    //--------------------------------------------------------------------------
    void ExprView::filter_previous_users(const EventFieldUsers &to_filter)
    //--------------------------------------------------------------------------
    {
      // Lock needs to be held by caller
      for (EventFieldUsers::const_iterator fit = to_filter.begin();
            fit != to_filter.end(); fit++)
      {
        EventFieldUsers::iterator event_finder = 
          previous_epoch_users.find(fit->first);
        // Might already have been pruned out
        if (event_finder == previous_epoch_users.end())
          continue;
        for (EventUsers::const_iterator it = fit->second.begin();
              it != fit->second.end(); it++)
        {
          EventUsers::iterator finder = event_finder->second.find(it->first);
          // Might already have been pruned out again
          if (finder == event_finder->second.end())
            continue;
          finder.filter(it->second);
          if (!finder->second)
          {
            if (finder->first->remove_reference())
              delete finder->first;
            event_finder->second.erase(finder);
          }
        }
        if (event_finder->second.empty())
          previous_epoch_users.erase(event_finder);
      }
    } 

    //--------------------------------------------------------------------------
    void ExprView::find_current_preconditions(const RegionUsage &usage,
                                              const FieldMask &user_mask,
                                              IndexSpaceExpression *user_expr,
                                              ApEvent term_event,
                                              const UniqueID op_id,
                                              const unsigned index,
                                              const bool user_covers,
                                              std::set<ApEvent> &preconditions,
                                              std::set<ApEvent> &dead_events,
                                              EventFieldUsers &filter_users,
                                              FieldMask &observed,
                                              FieldMask &non_dominated,
                                              const bool trace_recording)
    //--------------------------------------------------------------------------
    {
      // Caller must be holding the lock
      for (EventFieldUsers::const_iterator cit = current_epoch_users.begin(); 
            cit != current_epoch_users.end(); cit++)
      {
        if (cit->first == term_event)
          continue;
#ifndef LEGION_DISABLE_EVENT_PRUNING
        // We're about to do a bunch of expensive tests, 
        // so first do something cheap to see if we can 
        // skip all the tests.
        if (!trace_recording && cit->first.has_triggered_faultignorant())
        {
          dead_events.insert(cit->first);
          continue;
        }
#if 0
        // You might think you can optimize things like this, but you can't
        // because we still need the correct epoch users for every ExprView
        // when we go to add our user later
        if (!trace_recording &&
            preconditions.find(cit->first) != preconditions.end())
          continue;
#endif
#endif
        const EventUsers &event_users = cit->second;
        const FieldMask overlap = event_users.get_valid_mask() & user_mask;
        if (!overlap)
          continue;
        EventFieldUsers::iterator to_filter = filter_users.find(cit->first);
        for (EventUsers::const_iterator it = event_users.begin();
              it != event_users.end(); it++)
        {
          const FieldMask user_overlap = user_mask & it->second;
          if (!user_overlap)
            continue;
          bool dominates = true;
          if (has_local_precondition<false>(it->first, usage, user_expr,
                                      op_id, index, user_covers, dominates))
          {
            preconditions.insert(cit->first);
            if (dominates)
            {
              observed |= user_overlap;
              if (to_filter == filter_users.end())
              {
                filter_users[cit->first].insert(it->first, user_overlap);
                to_filter = filter_users.find(cit->first);
              }
              else
              {
#ifdef DEBUG_LEGION
                assert(to_filter->second.find(it->first) == 
                        to_filter->second.end());
#endif
                to_filter->second.insert(it->first, user_overlap);
              }
            }
            else
              non_dominated |= user_overlap;
          }
          else
            non_dominated |= user_overlap;
        }
      }
    }

    //--------------------------------------------------------------------------
    void ExprView::find_previous_preconditions(const RegionUsage &usage,
                                               const FieldMask &user_mask,
                                               IndexSpaceExpression *user_expr,
                                               ApEvent term_event,
                                               const UniqueID op_id,
                                               const unsigned index,
                                               const bool user_covers,
                                               std::set<ApEvent> &preconditions,
                                               std::set<ApEvent> &dead_events,
                                               const bool trace_recording)
    //--------------------------------------------------------------------------
    {
      // Caller must be holding the lock
      for (EventFieldUsers::const_iterator pit = previous_epoch_users.begin(); 
            pit != previous_epoch_users.end(); pit++)
      {
        if (pit->first == term_event)
          continue;
#ifndef LEGION_DISABLE_EVENT_PRUNING
        // We're about to do a bunch of expensive tests, 
        // so first do something cheap to see if we can 
        // skip all the tests.
        if (!trace_recording && pit->first.has_triggered_faultignorant())
        {
          dead_events.insert(pit->first);
          continue;
        }
#if 0
        // You might think you can optimize things like this, but you can't
        // because we still need the correct epoch users for every ExprView
        // when we go to add our user later
        if (!trace_recording &&
            preconditions.find(pit->first) != preconditions.end())
          continue;
#endif
#endif
        const EventUsers &event_users = pit->second;
        if (user_mask * event_users.get_valid_mask())
          continue;
        for (EventUsers::const_iterator it = event_users.begin();
              it != event_users.end(); it++)
        {
          if (user_mask * it->second)
            continue;
          if (has_local_precondition<false>(it->first, usage, user_expr,
                                                op_id, index, user_covers))
          {
            preconditions.insert(pit->first);
            break;
          }
        }
      }
    }

    //--------------------------------------------------------------------------
    void ExprView::find_current_preconditions(const RegionUsage &usage,
                                              const FieldMask &user_mask,
                                              IndexSpaceExpression *user_expr,
                                              const UniqueID op_id,
                                              const unsigned index,
                                              const bool user_covers,
                                              std::set<ApEvent> &preconditions,
                                              std::set<ApEvent> &dead_events,
                                              EventFieldUsers &filter_events,
                                              FieldMask &observed,
                                              FieldMask &non_dominated,
                                              const bool trace_recording)
    //--------------------------------------------------------------------------
    {
      // Caller must be holding the lock
      for (EventFieldUsers::const_iterator cit = current_epoch_users.begin(); 
            cit != current_epoch_users.end(); cit++)
      {
#ifndef LEGION_DISABLE_EVENT_PRUNING
        // We're about to do a bunch of expensive tests, 
        // so first do something cheap to see if we can 
        // skip all the tests.
        if (!trace_recording && cit->first.has_triggered_faultignorant())
        {
          dead_events.insert(cit->first);
          continue;
        }
#endif
        const EventUsers &event_users = cit->second;
        FieldMask overlap = event_users.get_valid_mask() & user_mask;
        if (!overlap)
          continue;
#if 0
        // You might think you can optimize things like this, but you can't
        // because we still need the correct epoch users for every ExprView
        // when we go to add our user later
        if (!trace_recording && finder != preconditions.end())
        {
          overlap -= finder->second.get_valid_mask();
          if (!overlap)
            continue;
        }
#endif
        EventFieldUsers::iterator to_filter = filter_events.find(cit->first);
        for (EventUsers::const_iterator it = event_users.begin();
              it != event_users.end(); it++)
        {
          const FieldMask user_overlap = user_mask & it->second;
          if (!user_overlap)
            continue;
          bool dominated = true;
          if (has_local_precondition<true>(it->first, usage, user_expr,
                                 op_id, index, user_covers, dominated)) 
          {
            preconditions.insert(cit->first);
            if (dominated)
            {
              observed |= user_overlap;
              if (to_filter == filter_events.end())
              {
                filter_events[cit->first].insert(it->first, user_overlap);
                to_filter = filter_events.find(cit->first);
              }
              else
                to_filter->second.insert(it->first, user_overlap);
            }
            else
              non_dominated |= user_overlap;
          }
          else
            non_dominated |= user_overlap;
        }
      }
    }

    //--------------------------------------------------------------------------
    void ExprView::find_previous_preconditions(const RegionUsage &usage,
                                               const FieldMask &user_mask,
                                               IndexSpaceExpression *user_expr,
                                               const UniqueID op_id,
                                               const unsigned index,
                                               const bool user_covers,
                                               std::set<ApEvent> &preconditions,
                                               std::set<ApEvent> &dead_events,
                                               const bool trace_recording)
    //--------------------------------------------------------------------------
    {
      // Caller must be holding the lock
      for (LegionMap<ApEvent,EventUsers>::const_iterator pit = 
            previous_epoch_users.begin(); pit != 
            previous_epoch_users.end(); pit++)
      {
#ifndef LEGION_DISABLE_EVENT_PRUNING
        // We're about to do a bunch of expensive tests, 
        // so first do something cheap to see if we can 
        // skip all the tests.
        if (!trace_recording && pit->first.has_triggered_faultignorant())
        {
          dead_events.insert(pit->first);
          continue;
        }
#endif
        const EventUsers &event_users = pit->second;
        FieldMask overlap = user_mask & event_users.get_valid_mask();
        if (!overlap)
          continue;
#if 0
        // You might think you can optimize things like this, but you can't
        // because we still need the correct epoch users for every ExprView
        // when we go to add our user later
        if (!trace_recording && finder != preconditions.end())
        {
          overlap -= finder->second.get_valid_mask();
          if (!overlap)
            continue;
        }
#endif
        for (EventUsers::const_iterator it = event_users.begin();
              it != event_users.end(); it++)
        {
          const FieldMask user_overlap = overlap & it->second;
          if (!user_overlap)
            continue;
          if (has_local_precondition<true>(it->first, usage, user_expr, 
                                           op_id, index, user_covers))
          {
            preconditions.insert(pit->first);
            break;
          }
        }
      }
    }

    //--------------------------------------------------------------------------
    void ExprView::find_current_preconditions(const RegionUsage &usage,
                                              const FieldMask &mask,
                                              IndexSpaceExpression *expr,
                                              const bool expr_covers,
                                              std::set<ApEvent> &last_events,
                                              FieldMask &observed,
                                              FieldMask &non_dominated) const
    //--------------------------------------------------------------------------
    {
      // Caller must be holding the lock
      for (EventFieldUsers::const_iterator cit = current_epoch_users.begin(); 
            cit != current_epoch_users.end(); cit++)
      {
        const EventUsers &event_users = cit->second;
        FieldMask overlap = event_users.get_valid_mask() & mask;
        if (!overlap)
          continue;
        for (EventUsers::const_iterator it = event_users.begin();
              it != event_users.end(); it++)
        {
          const FieldMask user_overlap = mask & it->second;
          if (!user_overlap)
            continue;
          bool dominated = true;
          // We're just reading these and we want to see all prior
          // dependences so just give dummy opid and index
          if (has_local_precondition<true>(it->first, usage, expr,
                   0/*opid*/, 0/*index*/, expr_covers, dominated)) 
          {
            last_events.insert(cit->first);
            if (dominated)
              observed |= user_overlap;
            else
              non_dominated |= user_overlap;
          }
          else
            non_dominated |= user_overlap;
        }
      }
    }

    //--------------------------------------------------------------------------
    void ExprView::find_previous_preconditions(const RegionUsage &usage,
                                            const FieldMask &mask,
                                            IndexSpaceExpression *expr,
                                            const bool expr_covers,
                                            std::set<ApEvent> &last_users) const
    //--------------------------------------------------------------------------
    {
      // Caller must be holding the lock
      for (LegionMap<ApEvent,EventUsers>::const_iterator pit = 
            previous_epoch_users.begin(); pit != 
            previous_epoch_users.end(); pit++)
      {
        const EventUsers &event_users = pit->second;
        FieldMask overlap = mask & event_users.get_valid_mask();
        if (!overlap)
          continue;
        for (EventUsers::const_iterator it = event_users.begin();
              it != event_users.end(); it++)
        {
          const FieldMask user_overlap = overlap & it->second;
          if (!user_overlap)
            continue;
          // We're just reading these and we want to see all prior
          // dependences so just give dummy opid and index
          if (has_local_precondition<true>(it->first, usage, expr, 
                               0/*opid*/, 0/*index*/, expr_covers))
          {
            last_users.insert(pit->first);
            break;
          }
        }
      }
    }

    //--------------------------------------------------------------------------
    void ExprView::find_previous_filter_users(const FieldMask &dom_mask,
                                            EventFieldUsers &filter_users)
    //--------------------------------------------------------------------------
    {
      // Lock better be held by caller
      for (EventFieldUsers::const_iterator pit = previous_epoch_users.begin(); 
            pit != previous_epoch_users.end(); pit++)
      {
        FieldMask event_overlap = pit->second.get_valid_mask() & dom_mask;
        if (!event_overlap)
          continue;
        EventFieldUsers::iterator to_filter = filter_users.find(pit->first);
        for (EventUsers::const_iterator it = pit->second.begin();
              it != pit->second.end(); it++)
        {
          const FieldMask user_overlap = it->second & event_overlap;
          if (!user_overlap)
            continue;
          if (to_filter == filter_users.end())
          {
            filter_users[pit->first].insert(it->first, user_overlap);
            to_filter = filter_users.find(pit->first);
          }
          else
            to_filter->second.insert(it->first, user_overlap);
        }
      }
    }

    /////////////////////////////////////////////////////////////
    // PendingTaskUser
    /////////////////////////////////////////////////////////////

    //--------------------------------------------------------------------------
    PendingTaskUser::PendingTaskUser(const RegionUsage &u, const FieldMask &m,
                                     IndexSpaceNode *expr, const UniqueID id,
                                     const unsigned idx, const ApEvent term,
                                     const RtEvent collect)
      : usage(u), user_mask(m), user_expr(expr), op_id(id), 
        index(idx), term_event(term), collect_event(collect)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    PendingTaskUser::~PendingTaskUser(void)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    bool PendingTaskUser::apply(MaterializedView *view, const FieldMask &mask)
    //--------------------------------------------------------------------------
    {
      const FieldMask overlap = user_mask & mask;
      if (!overlap)
        return false;
      view->add_internal_task_user(usage, user_expr, overlap, term_event, 
                                   collect_event, op_id,index,false/*tracing*/);
      user_mask -= overlap;
      return !user_mask;
    }

    /////////////////////////////////////////////////////////////
    // PendingCopyUser
    /////////////////////////////////////////////////////////////

    //--------------------------------------------------------------------------
    PendingCopyUser::PendingCopyUser(const bool read, const FieldMask &mask,
                                     IndexSpaceExpression *e, const UniqueID id,
                                     const unsigned idx, const ApEvent term,
                                     const RtEvent collect)
      : reading(read), copy_mask(mask), copy_expr(e), op_id(id), 
        index(idx), term_event(term), collect_event(collect)
    //--------------------------------------------------------------------------
    {
    }
    
    //--------------------------------------------------------------------------
    PendingCopyUser::~PendingCopyUser(void)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    bool PendingCopyUser::apply(MaterializedView *view, const FieldMask &mask)
    //--------------------------------------------------------------------------
    {
      const FieldMask overlap = copy_mask & mask;
      if (!overlap)
        return false;
      const RegionUsage usage(reading ? LEGION_READ_ONLY : LEGION_READ_WRITE, 
                              LEGION_EXCLUSIVE, 0);
      view->add_internal_copy_user(usage, copy_expr, overlap, term_event,
                       collect_event, op_id, index, false/*trace recording*/);
      copy_mask -= overlap;
      return !copy_mask;
    }

    /////////////////////////////////////////////////////////////
    // IndividualView 
    /////////////////////////////////////////////////////////////

    //--------------------------------------------------------------------------
    IndividualView::IndividualView(RegionTreeForest *ctx, DistributedID did,
                     PhysicalManager *man, AddressSpaceID owner_proc,
                     AddressSpaceID log_owner, UniqueID owner_context,
                     bool register_now, CollectiveMapping *mapping)
      : InstanceView(ctx, did, owner_proc, owner_context, register_now,mapping),
        manager(man), logical_owner(log_owner)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(manager != NULL);
#endif
      // Keep the manager from being collected
      manager->add_nested_resource_ref(did);
    }

    //--------------------------------------------------------------------------
    IndividualView::~IndividualView(void)
    //--------------------------------------------------------------------------
    {
      if (manager->remove_nested_resource_ref(did))
        delete manager;
      if (is_owner())
      {
        for (std::map<unsigned,Reservation>::iterator it =
              view_reservations.begin(); it != view_reservations.end(); it++)
          it->second.destroy_reservation();
      }
    }

    //--------------------------------------------------------------------------
    AddressSpaceID IndividualView::get_analysis_space(
                                                PhysicalManager *instance) const
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(instance == manager);
#endif
      return logical_owner;
    }

    //--------------------------------------------------------------------------
    void IndividualView::notify_active(ReferenceMutator *mutator)
    //--------------------------------------------------------------------------
    {
      manager->add_nested_gc_ref(did, mutator);
      // If we're the logical owner, but not the original owner
      // then we use a gc reference on the original owner to 
      // keep all the views allive until we're done
      if (is_logical_owner() && !is_owner())
        send_remote_gc_increment(owner_space, mutator);
    }

    //--------------------------------------------------------------------------
    void IndividualView::notify_inactive(ReferenceMutator *mutator)
    //--------------------------------------------------------------------------
    {
      manager->remove_nested_gc_ref(did, mutator);
      // If we're the logical owner but not the original owner
      // then we remove the gc reference that we added
      if (is_logical_owner() && !is_owner())
        send_remote_gc_decrement(owner_space, mutator);
    }

    //--------------------------------------------------------------------------
    void IndividualView::notify_valid(ReferenceMutator *mutator)
    //--------------------------------------------------------------------------
    {
      // The logical owner is where complete set of users is and is therefore
      // where garbage collection will take place so we need to send our 
      // valid update there if we're not the owner, otherwise we send it 
      // down to the manager
      if (is_logical_owner())
        manager->add_nested_valid_ref(did, mutator);
      else
        send_remote_valid_increment(logical_owner, mutator);
    }

    //--------------------------------------------------------------------------
    void IndividualView::notify_invalid(ReferenceMutator *mutator)
    //--------------------------------------------------------------------------
    {
      if (is_logical_owner())
        // we have a resource reference on the manager so no need to check
        manager->remove_nested_valid_ref(did, mutator);
      else
        send_remote_valid_decrement(logical_owner, mutator);
    }

    //--------------------------------------------------------------------------
    ApEvent IndividualView::register_collective_user(const RegionUsage &usage,
                                         const FieldMask &user_mask,
                                         IndexSpaceNode *expr,
                                         const UniqueID op_id,
                                         const size_t op_ctx_index,
                                         const unsigned index,
                                         ApEvent term_event,
                                         RtEvent collect_event,
                                         PhysicalManager *target,
                                         size_t local_collective_arrivals,
                                         std::set<RtEvent> &applied_events,
                                         const PhysicalTraceInfo &trace_info,
                                         const bool symbolic)
    //--------------------------------------------------------------------------
    {
      // Somewhat strangely we can still get calls to this method in cases
      // with control replication for things like acquire/release on individual
      // managers that represent file instances. In this case we'll just have
      // a single node perform the view analysis and then we broadcast out the
      // resulting event out to all the participants. 
#ifdef DEBUG_LEGION
      assert(collective_mapping != NULL);
      assert(collective_mapping->contains(local_space));
#endif
      // First we need to decide which node is going to be the owner node
      // We'll prefer it to be the logical view owner since that is where
      // the event will be produced, otherwise, we'll just pick whichever
      // is closest to the logical view node
      const AddressSpaceID origin = 
        collective_mapping->contains(logical_owner) ? logical_owner : 
        collective_mapping->find_nearest(logical_owner);
      ApUserEvent result;
      RtUserEvent registered;
      std::vector<ApEvent> term_events;
      PhysicalTraceInfo *result_info = NULL;
      const RendezvousKey key(op_ctx_index, index);
      {
        AutoLock v_lock(view_lock);
        // Check to see if we're the first one to arrive on this node
        std::map<RendezvousKey,UserRendezvous>::iterator finder =
          rendezvous_users.find(key);
        if (finder == rendezvous_users.end())
        {
          // If we are then make the record for knowing when we've seen
          // all the expected arrivals
          finder = rendezvous_users.insert(
              std::make_pair(key,UserRendezvous())).first; 
          UserRendezvous &rendezvous = finder->second;
          rendezvous.remaining_local_arrivals = local_collective_arrivals;
          rendezvous.local_initialized = true;
          rendezvous.remaining_remote_arrivals =
            collective_mapping->count_children(origin, local_space);
          rendezvous.ready_event = Runtime::create_ap_user_event(&trace_info);
          rendezvous.trace_info = new PhysicalTraceInfo(trace_info);
          rendezvous.registered = Runtime::create_rt_user_event();
        }
        else if (!finder->second.local_initialized)
        {
#ifdef DEBUG_LEGION
          assert(!finder->second.ready_event.exists());
          assert(finder->second.trace_info == NULL);
#endif
          // First local arrival
          finder->second.remaining_local_arrivals = local_collective_arrivals;
          finder->second.ready_event =
            Runtime::create_ap_user_event(&trace_info);
          finder->second.trace_info = new PhysicalTraceInfo(trace_info);
          if (!finder->second.remote_ready_events.empty())
          {
            for (std::map<ApUserEvent,PhysicalTraceInfo*>::const_iterator it =
                  finder->second.remote_ready_events.begin(); it !=
                  finder->second.remote_ready_events.end(); it++)
            {
              Runtime::trigger_event(it->second, it->first, 
                                finder->second.ready_event);
              delete it->second;
            }
            finder->second.remote_ready_events.clear();
          }
        }
        result = finder->second.ready_event;
        result_info = finder->second.trace_info;
        registered = finder->second.registered;
        applied_events.insert(registered);
        if (term_event.exists())
          finder->second.term_events.push_back(term_event);
#ifdef DEBUG_LEGION
        assert(finder->second.local_initialized);
        assert(finder->second.remaining_local_arrivals > 0);
#endif
        // If we're still expecting arrivals then nothing to do yet
        if ((--finder->second.remaining_local_arrivals > 0) ||
            (finder->second.remaining_remote_arrivals > 0))
        {
          // We need to save the trace info no matter what
          if (finder->second.mask == NULL)
          {
            if (local_space == origin)
            {
              // Save our state for performing the registration later
              finder->second.usage = usage;
              finder->second.mask = new FieldMask(user_mask);
              finder->second.expr = expr;
              WrapperReferenceMutator mutator(applied_events);
              expr->add_nested_expression_reference(did, &mutator);
              finder->second.op_id = op_id;
              finder->second.collect_event = collect_event;
              finder->second.symbolic = symbolic;
            }
            else
            {
              finder->second.applied = Runtime::create_rt_user_event();
              applied_events.insert(finder->second.applied);
            }
          }
          else if (local_space != origin)
          {
#ifdef DEBUG_LEGION
            assert(finder->second.applied.exists());
#endif
            applied_events.insert(finder->second.applied);
          }
          return result;
        }
        term_events.swap(finder->second.term_events);
#ifdef DEBUG_LEGION
        assert(finder->second.remote_ready_events.empty());
#endif
        // We're done with our entry after this so no need to keep it
        rendezvous_users.erase(finder);
      }
      if (!term_events.empty())
        term_event = Runtime::merge_events(&trace_info, term_events);
      if (local_space != origin)
      {
        const AddressSpaceID parent = 
          collective_mapping->get_parent(origin, local_space);
        Serializer rez;
        {
          RezCheck z(rez);
          rez.serialize(did);
          rez.serialize(op_ctx_index);
          rez.serialize(index);
          rez.serialize(origin);
          result_info->pack_trace_info(rez, applied_events);
          rez.serialize(term_event);
          rez.serialize(result);
          rez.serialize(registered);
        }
        runtime->send_collective_individual_register_user(parent, rez);
      }
      else
      {
        std::set<RtEvent> registered_events; 
        const ApEvent ready = register_user(usage, user_mask, expr, op_id,
            op_ctx_index, index, term_event, collect_event, target, 
            0/*no collective arrivals*/, registered_events, *result_info,
            runtime->address_space, symbolic);
        Runtime::trigger_event(result_info, result, ready);
        if (!registered_events.empty())
          Runtime::trigger_event(registered,
              Runtime::merge_events(registered_events));
        else
          Runtime::trigger_event(registered);
      }
      delete result_info;
      return result;
    }

    //--------------------------------------------------------------------------
    void IndividualView::process_collective_user_registration(
                                            const size_t op_ctx_index,
                                            const unsigned index,
                                            const AddressSpaceID origin,
                                            const PhysicalTraceInfo &trace_info,
                                            ApEvent remote_term_event,
                                            ApUserEvent remote_ready_event,
                                            RtUserEvent remote_registered)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(collective_mapping != NULL);
#endif
      UserRendezvous to_perform;
      const RendezvousKey key(op_ctx_index, index);
      {
        AutoLock v_lock(view_lock);
        // Check to see if we're the first one to arrive on this node
        std::map<RendezvousKey,UserRendezvous>::iterator finder =
          rendezvous_users.find(key);
        if (finder == rendezvous_users.end())
        {
          // If we are then make the record for knowing when we've seen
          // all the expected arrivals
          finder = rendezvous_users.insert(
              std::make_pair(key,UserRendezvous())).first; 
          UserRendezvous &rendezvous = finder->second;
          rendezvous.local_initialized = false;
          rendezvous.remaining_remote_arrivals =
            collective_mapping->count_children(origin, local_space);
          // Don't make the ready event, that needs to be done with a
          // local trace_info
          rendezvous.registered = Runtime::create_rt_user_event();
        }
        if (remote_term_event.exists())
          finder->second.term_events.push_back(remote_term_event);
        Runtime::trigger_event(remote_registered, finder->second.registered);
        if (!finder->second.ready_event.exists())
          finder->second.remote_ready_events[remote_ready_event] =
            new PhysicalTraceInfo(trace_info);
        else
          Runtime::trigger_event(&trace_info, remote_ready_event, 
                                 finder->second.ready_event);
#ifdef DEBUG_LEGION
        assert(finder->second.remaining_remote_arrivals > 0);
#endif
        // Check to see if we've done all the arrivals
        if ((--finder->second.remaining_remote_arrivals > 0) ||
            !finder->second.local_initialized ||
            (finder->second.remaining_local_arrivals > 0))
          return;
#ifdef DEBUG_LEGION
        assert(finder->second.remote_ready_events.empty());
        assert(finder->second.trace_info != NULL);
#endif
        // Last needed arrival, see if we're the origin or not
        to_perform = std::move(finder->second);
        rendezvous_users.erase(finder);
      }
      ApEvent term_event;
      if (!to_perform.term_events.empty())
        term_event =
          Runtime::merge_events(to_perform.trace_info, to_perform.term_events);
      if (local_space != origin)
      {
#ifdef DEBUG_LEGION
        assert(to_perform.applied.exists());
#endif
        // Send the message to the parent
        const AddressSpaceID parent = 
            collective_mapping->get_parent(origin, local_space);
        std::set<RtEvent> applied_events;
        Serializer rez;
        {
          RezCheck z(rez);
          rez.serialize(did);
          rez.serialize(op_ctx_index);
          rez.serialize(index);
          rez.serialize(origin);
          to_perform.trace_info->pack_trace_info(rez, applied_events);
          rez.serialize(term_event);
          rez.serialize(to_perform.ready_event);
          rez.serialize(to_perform.registered);
        }
        runtime->send_collective_individual_register_user(parent, rez);
        if (!applied_events.empty())
          Runtime::trigger_event(to_perform.applied,
              Runtime::merge_events(applied_events));
        else
          Runtime::trigger_event(to_perform.applied);
      }
      else
      {
#ifdef DEBUG_LEGION
        assert(!to_perform.applied.exists());
#endif
        std::set<RtEvent> registered_events;
        const ApEvent ready = register_user(to_perform.usage,
            *to_perform.mask, to_perform.expr, to_perform.op_id, op_ctx_index,
            index, term_event, to_perform.collect_event, manager,
            0/*no collective arrivals*/, registered_events,
            *to_perform.trace_info, runtime->address_space,to_perform.symbolic);
        Runtime::trigger_event(to_perform.trace_info, 
                      to_perform.ready_event, ready);
        if (!registered_events.empty())
          Runtime::trigger_event(to_perform.registered,
              Runtime::merge_events(registered_events));
        else
          Runtime::trigger_event(to_perform.registered);
        if (to_perform.expr->remove_nested_expression_reference(did))
          delete to_perform.expr;
        delete to_perform.mask;
      }
      delete to_perform.trace_info;
    }

    //--------------------------------------------------------------------------
    /*static*/ void IndividualView::handle_collective_user_registration(
                                          Runtime *runtime, Deserializer &derez)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez);
      DistributedID did;
      derez.deserialize(did);
      RtEvent ready;
      IndividualView *view = static_cast<IndividualView*>(
              runtime->find_or_request_logical_view(did, ready));
      size_t op_ctx_index;
      derez.deserialize(op_ctx_index);
      unsigned index;
      derez.deserialize(index);
      AddressSpaceID origin;
      derez.deserialize(origin);
      PhysicalTraceInfo trace_info = 
        PhysicalTraceInfo::unpack_trace_info(derez, runtime); 
      ApEvent term_event;
      derez.deserialize(term_event);
      ApUserEvent ready_event;
      derez.deserialize(ready_event);
      RtUserEvent registered_event;
      derez.deserialize(registered_event);

      if (ready.exists() && !ready.has_triggered())
        ready.wait();

      view->process_collective_user_registration(op_ctx_index, index, origin,
                      trace_info, term_event, ready_event, registered_event);
    }

    //--------------------------------------------------------------------------
    void IndividualView::find_atomic_reservations(PhysicalManager *instance,
                const FieldMask &mask, Operation *op, unsigned index, bool excl)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(instance == manager);
#endif
      std::vector<Reservation> reservations;
      find_field_reservations(mask, reservations); 
      for (unsigned idx = 0; idx < reservations.size(); idx++)
        op->update_atomic_locks(index, reservations[idx], excl);
    } 

    //--------------------------------------------------------------------------
    void IndividualView::find_field_reservations(const FieldMask &mask,
                                         std::vector<Reservation> &reservations)
    //--------------------------------------------------------------------------
    {
      const RtEvent ready = 
        find_field_reservations(mask, &reservations, runtime->address_space);
      if (ready.exists() && !ready.has_triggered())
        ready.wait();
      // Sort them into order if necessary
      if (reservations.size() > 1)
        std::sort(reservations.begin(), reservations.end());
    }

    //--------------------------------------------------------------------------
    RtEvent IndividualView::find_field_reservations(const FieldMask &mask,
                               std::vector<Reservation> *reservations,
                               AddressSpaceID source, RtUserEvent to_trigger)
    //--------------------------------------------------------------------------
    {
      std::vector<Reservation> results;
      if (is_owner())
      {
        results.reserve(mask.pop_count());
        // We're the owner so we can make all the fields
        AutoLock v_lock(view_lock);
        for (int idx = mask.find_first_set(); idx >= 0;
              idx = mask.find_next_set(idx+1))
        {
          std::map<unsigned,Reservation>::const_iterator finder =
            view_reservations.find(idx);
          if (finder == view_reservations.end())
          {
            // Make a new reservation and add it to the set
            Reservation handle = Reservation::create_reservation();
            view_reservations[idx] = handle;
            results.push_back(handle);
          }
          else
            results.push_back(finder->second);
        }
      }
      else
      {
        // See if we can find them all locally
        {
          AutoLock v_lock(view_lock, 1, false/*exclusive*/);
          for (int idx = mask.find_first_set(); idx >= 0;
                idx = mask.find_next_set(idx+1))
          {
            std::map<unsigned,Reservation>::const_iterator finder =
              view_reservations.find(idx);
            if (finder != view_reservations.end())
              results.push_back(finder->second);
            else
              break;
          }
        }
        if (results.size() < mask.pop_count())
        {
          // Couldn't find them all so send the request to the owner
          if (!to_trigger.exists())
            to_trigger = Runtime::create_rt_user_event();
          Serializer rez;
          {
            RezCheck z(rez);
            rez.serialize(did);
            rez.serialize(mask);
            rez.serialize(reservations);
            rez.serialize(source);
            rez.serialize(to_trigger);
          }
          runtime->send_atomic_reservation_request(owner_space, rez);
          return to_trigger;
        }
      }
      if (source != local_space)
      {
#ifdef DEBUG_LEGION
        assert(to_trigger.exists());
#endif
        // Send the result back to the source
        Serializer rez;
        {
          RezCheck z(rez);
          rez.serialize(did);
          rez.serialize(mask);
          rez.serialize(reservations);
          rez.serialize<size_t>(results.size());
          for (std::vector<Reservation>::const_iterator it =
                results.begin(); it != results.end(); it++)
            rez.serialize(*it);
          rez.serialize(to_trigger);
        }
        runtime->send_atomic_reservation_response(source, rez);
      }
      else
      {
        reservations->swap(results);
        if (to_trigger.exists())
          Runtime::trigger_event(to_trigger);
      }
      return to_trigger;
    }

    //--------------------------------------------------------------------------
    void IndividualView::update_field_reservations(const FieldMask &mask,
                                   const std::vector<Reservation> &reservations)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(!is_owner());
#endif
      AutoLock v_lock(view_lock);
      unsigned offset = 0;
      for (int idx = mask.find_first_set(); idx >= 0;
            idx = mask.find_next_set(idx+1))
        view_reservations[idx] = reservations[offset++];
    }

    //--------------------------------------------------------------------------
    /*static*/ void IndividualView::handle_atomic_reservation_request(
                                          Runtime *runtime, Deserializer &derez)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez);
      DistributedID did;
      derez.deserialize(did);
      RtEvent ready;
      IndividualView *view = static_cast<IndividualView*>(
        runtime->find_or_request_logical_view(did, ready));
      FieldMask mask;
      derez.deserialize(mask);
      std::vector<Reservation> *target;
      derez.deserialize(target);
      AddressSpaceID source;
      derez.deserialize(source);
      RtUserEvent to_trigger;
      derez.deserialize(to_trigger);

      if (ready.exists() && !ready.has_triggered())
        ready.wait();
      view->find_field_reservations(mask, target, source, to_trigger);
    }

    //--------------------------------------------------------------------------
    /*static*/ void IndividualView::handle_atomic_reservation_response(
                                          Runtime *runtime, Deserializer &derez)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez);
      DistributedID did;
      derez.deserialize(did);
      RtEvent ready;
      IndividualView *view = static_cast<IndividualView*>(
        runtime->find_or_request_logical_view(did, ready));
      FieldMask mask;
      derez.deserialize(mask);
      std::vector<Reservation> *target;
      derez.deserialize(target);
      size_t num_reservations;
      derez.deserialize(num_reservations);
      target->resize(num_reservations);
      for (unsigned idx = 0; idx < num_reservations; idx++)
        derez.deserialize((*target)[idx]);
      if (ready.exists() && !ready.has_triggered())
        ready.wait();
      view->update_field_reservations(mask, *target);
      RtUserEvent to_trigger;
      derez.deserialize(to_trigger);
      Runtime::trigger_event(to_trigger);
    }

    //--------------------------------------------------------------------------
    /*static*/ void IndividualView::handle_view_find_copy_pre_request(
                   Deserializer &derez, Runtime *runtime, AddressSpaceID source)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez);
      DistributedID did;
      derez.deserialize(did);
      RtEvent ready = RtEvent::NO_RT_EVENT;
      LogicalView *view = runtime->find_or_request_logical_view(did, ready);

      bool reading;
      derez.deserialize<bool>(reading);
      ReductionOpID redop;
      derez.deserialize(redop);
      FieldMask copy_mask;
      derez.deserialize(copy_mask);
      IndexSpaceExpression *copy_expr = 
        IndexSpaceExpression::unpack_expression(derez, runtime->forest, source);
      UniqueID op_id;
      derez.deserialize(op_id);
      unsigned index;
      derez.deserialize(index);
      ApUserEvent to_trigger;
      derez.deserialize(to_trigger);
      RtUserEvent applied;
      derez.deserialize(applied);
      std::set<RtEvent> applied_events;
      const PhysicalTraceInfo trace_info = 
        PhysicalTraceInfo::unpack_trace_info(derez, runtime);

      // This blocks the virtual channel, but keeps queries in-order 
      // with respect to updates from the same node which is necessary
      // for preventing cycles in the realm event graph
      if (ready.exists() && !ready.has_triggered())
        ready.wait();
      IndividualView *inst_view = view->as_individual_view();
      const ApEvent pre = inst_view->find_copy_preconditions(reading, redop,
          copy_mask, copy_expr, op_id, index, applied_events, trace_info);
      Runtime::trigger_event(&trace_info, to_trigger, pre);
      if (!applied_events.empty())
        Runtime::trigger_event(applied, Runtime::merge_events(applied_events));
      else
        Runtime::trigger_event(applied);
    }

    //--------------------------------------------------------------------------
    /*static*/ void IndividualView::handle_view_add_copy_user(
                   Deserializer &derez, Runtime *runtime, AddressSpaceID source)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez);
      DistributedID did;
      derez.deserialize(did);
      RtEvent ready = RtEvent::NO_RT_EVENT;
      LogicalView *view = runtime->find_or_request_logical_view(did, ready);

      bool reading;
      derez.deserialize(reading);
      ReductionOpID redop;
      derez.deserialize(redop);
      ApEvent term_event;
      derez.deserialize(term_event);
      RtEvent collect_event;
      derez.deserialize(collect_event);
      FieldMask copy_mask;
      derez.deserialize(copy_mask);
      IndexSpaceExpression *copy_expr =
        IndexSpaceExpression::unpack_expression(derez, runtime->forest, source);
      UniqueID op_id;
      derez.deserialize(op_id);
      unsigned index;
      derez.deserialize(index);
      RtUserEvent applied_event;
      derez.deserialize(applied_event);
      bool trace_recording;
      derez.deserialize(trace_recording);

      if (ready.exists() && !ready.has_triggered())
        ready.wait();
#ifdef DEBUG_LEGION
      assert(view->is_individual_view());
#endif
      IndividualView *inst_view = view->as_individual_view();

      std::set<RtEvent> applied_events;
      inst_view->add_copy_user(reading,redop,term_event,collect_event,copy_mask,
          copy_expr, op_id, index, applied_events, trace_recording, source);
      if (!applied_events.empty())
      {
        const RtEvent precondition = Runtime::merge_events(applied_events);
        Runtime::trigger_event(applied_event, precondition);
        // Send back a response to the source removing the remote valid ref
        if (inst_view->is_logical_owner())
          inst_view->send_remote_valid_decrement(source, NULL, precondition);
      }
      else
      {
        Runtime::trigger_event(applied_event);
        // Send back a response to the source removing the remote valid ref
        if (inst_view->is_logical_owner())
          inst_view->send_remote_valid_decrement(source);
      }
    }

    //--------------------------------------------------------------------------
    void IndividualView::handle_view_find_last_users_request(
                   Deserializer &derez, Runtime *runtime, AddressSpaceID source)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez);
      DistributedID did;
      derez.deserialize(did);
      RtEvent ready;
      LogicalView *view = runtime->find_or_request_logical_view(did, ready);
      DistributedID manager_did;
      derez.deserialize(manager_did);
      RtEvent manager_ready;
      PhysicalManager *manager =
        runtime->find_or_request_instance_manager(manager_did, manager_ready);

      std::vector<ApEvent> *target;
      derez.deserialize(target);
      RegionUsage usage;
      derez.deserialize(usage);
      FieldMask mask;
      derez.deserialize(mask);
      IndexSpaceExpression *expr =
        IndexSpaceExpression::unpack_expression(derez, runtime->forest, source);
      RtUserEvent done;
      derez.deserialize(done);

      std::set<ApEvent> result;
      std::vector<RtEvent> applied;
      if (ready.exists() && !ready.has_triggered())
        ready.wait();
      if (manager_ready.exists() && !manager_ready.has_triggered())
        manager_ready.wait();
#ifdef DEBUG_LEGION
      assert(view->is_individual_view());
#endif
      IndividualView *inst_view = view->as_individual_view();
      inst_view->find_last_users(manager, result, usage, mask, expr, applied);
      if (!result.empty())
      {
        Serializer rez;
        {
          RezCheck z2(rez);
          rez.serialize(target);
          rez.serialize<size_t>(result.size());
          for (std::set<ApEvent>::const_iterator it =
                result.begin(); it != result.end(); it++)
            rez.serialize(*it);
          rez.serialize(done);
          if (!applied.empty())
            rez.serialize(Runtime::merge_events(applied));
          else
            rez.serialize(RtEvent::NO_RT_EVENT);
        }
        runtime->send_view_find_last_users_response(source, rez);
      }
      else
      {
        if (!applied.empty())
          Runtime::trigger_event(done, Runtime::merge_events(applied));
        else
          Runtime::trigger_event(done);
      }
    }

    //--------------------------------------------------------------------------
    void IndividualView::handle_view_find_last_users_response(
                                                            Deserializer &derez)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez);
      std::set<ApEvent> *target;
      derez.deserialize(target);
      size_t num_events;
      derez.deserialize(num_events);
      for (unsigned idx = 0; idx < num_events; idx++)
      {
        ApEvent event;
        derez.deserialize(event);
        target->insert(event);
      }
      RtUserEvent done;
      derez.deserialize(done);
      RtEvent pre;
      derez.deserialize(pre);
      Runtime::trigger_event(done, pre);
    }

    /////////////////////////////////////////////////////////////
    // MaterializedView 
    /////////////////////////////////////////////////////////////

    //--------------------------------------------------------------------------
    MaterializedView::MaterializedView(
                               RegionTreeForest *ctx, DistributedID did,
                               AddressSpaceID own_addr,
                               AddressSpaceID log_own, PhysicalManager *man,
                               UniqueID own_ctx, bool register_now,
                               CollectiveMapping *mapping)
      : IndividualView(ctx, encode_materialized_did(did), man, own_addr,
                       log_own, own_ctx, register_now, mapping), 
        expr_cache_uses(0), outstanding_additions(0)
#ifdef ENABLE_VIEW_REPLICATION
        , remote_added_users(0), remote_pending_users(NULL)
#endif
    //--------------------------------------------------------------------------
    {
#ifdef ENABLE_VIEW_REPLICATION
      repl_ptr.replicated_copies = NULL;
#endif
      if (is_logical_owner())
      {
        current_users = new ExprView(ctx,manager,this,manager->instance_domain);
        current_users->add_reference();
      }
      else
        current_users = NULL;
#ifdef LEGION_GC
      log_garbage.info("GC Materialized View %lld %d %lld", 
          LEGION_DISTRIBUTED_ID_FILTER(this->did), local_space, 
          LEGION_DISTRIBUTED_ID_FILTER(manager->did)); 
#endif
    }

    //--------------------------------------------------------------------------
    MaterializedView::~MaterializedView(void)
    //--------------------------------------------------------------------------
    {
      if ((current_users != NULL) && current_users->remove_reference())
        delete current_users;
#ifdef ENABLE_VIEW_REPLICATION
      if (repl_ptr.replicated_copies != NULL)
      {
#ifdef DEBUG_LEGION
        assert(is_logical_owner());
#endif
        // We should only have replicated copies here
        // If there are replicated requests that is very bad
        delete repl_ptr.replicated_copies;
      }
#ifdef DEBUG_LEGION
      assert(remote_pending_users == NULL);
#endif
#endif
    }

    //--------------------------------------------------------------------------
    const FieldMask& MaterializedView::get_physical_mask(void) const
    //--------------------------------------------------------------------------
    {
      return manager->layout->allocated_fields;
    }

    //--------------------------------------------------------------------------
    bool MaterializedView::has_space(const FieldMask &space_mask) const
    //--------------------------------------------------------------------------
    {
      return !(space_mask - manager->layout->allocated_fields);
    }

    //--------------------------------------------------------------------------
    void MaterializedView::add_initial_user(ApEvent term_event,
                                            const RegionUsage &usage,
                                            const FieldMask &user_mask,
                                            IndexSpaceExpression *user_expr,
                                            const UniqueID op_id,
                                            const unsigned index)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(is_logical_owner());
      assert(current_users != NULL);
#endif
#ifdef ENABLE_VIEW_REPLICATION
      PhysicalUser *user = new PhysicalUser(usage, user_expr, op_id, index, 
                                term_event, false/*copy user*/, true/*covers*/);
#else
      PhysicalUser *user = new PhysicalUser(usage, user_expr, op_id, index, 
                                            false/*copy user*/, true/*covers*/);
#endif
      // No need to take the lock since we are just initializing
      // If it's the root this is easy
      if (user_expr == current_users->view_expr)
      {
        current_users->add_current_user(user, term_event, RtEvent::NO_RT_EVENT,
                                        user_mask, false);
        return;
      }
      // See if we have it in the cache
      std::map<IndexSpaceExprID,ExprView*>::const_iterator finder = 
        expr_cache.find(user_expr->expr_id);
      if (finder == expr_cache.end() || 
          !(finder->second->invalid_fields * user_mask))
      {
        // No need for expr_lock since this is initialization
        if (finder == expr_cache.end())
        {
          ExprView *target_view = current_users->find_congruent_view(user_expr);
          // Couldn't find a congruent view so we need to make one
          if (target_view == NULL)
            target_view = new ExprView(context, manager, this, user_expr);
          expr_cache[user_expr->expr_id] = target_view;
          finder = expr_cache.find(user_expr->expr_id);
        }
        if (finder->second != current_users)
        {
          // Now insert it for the invalid fields
          FieldMask insert_mask = user_mask & finder->second->invalid_fields;
          // Mark that we're removing these fields from the invalid fields
          // first since we're later going to destroy the insert mask
          finder->second->invalid_fields -= insert_mask;
          // Then insert the subview into the tree
          current_users->insert_subview(finder->second, insert_mask);
        }
      }
      // Now that the view is valid we can add the user to it
      finder->second->add_current_user(user, term_event, RtEvent::NO_RT_EVENT,
                                       user_mask, false);
      // No need to launch a collection task as the destructor will handle it 
    }

    //--------------------------------------------------------------------------
    ApEvent MaterializedView::register_user(const RegionUsage &usage,
                                         const FieldMask &user_mask,
                                         IndexSpaceNode *user_expr,
                                         const UniqueID op_id,
                                         const size_t op_ctx_index,
                                         const unsigned index,
                                         ApEvent term_event,
                                         RtEvent collect_event,
                                         PhysicalManager *target,
                                         size_t local_collective_arrivals,
                                         std::set<RtEvent> &applied_events,
                                         const PhysicalTraceInfo &trace_info,
                                         const AddressSpaceID source,
                                         const bool symbolic /*=false*/)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(target == manager);
#endif
      // Handle the collective rendezvous if necessary
      if (local_collective_arrivals > 0)
        return register_collective_user(usage, user_mask, user_expr,
              op_id, op_ctx_index, index, term_event, collect_event,
              target, local_collective_arrivals, applied_events,
              trace_info, symbolic);
      // Quick test for empty index space expressions
      if (!symbolic && user_expr->is_empty())
        return manager->get_use_event(term_event);
      if (!is_logical_owner())
      {
        ApUserEvent ready_event;
        // Check to see if this user came from somewhere that wasn't
        // the logical owner, if so we need to send the update back 
        // to the owner to be handled
        if (source != logical_owner)
        {
          // If we're not the logical owner send a message there 
          // to do the analysis and provide a user event to trigger
          // with the precondition
          ready_event = Runtime::create_ap_user_event(&trace_info);
          RtUserEvent applied_event = Runtime::create_rt_user_event();
          Serializer rez;
          {
            RezCheck z(rez);
            rez.serialize(did);
            rez.serialize(target->did);
            rez.serialize(usage);
            rez.serialize(user_mask);
            rez.serialize(user_expr->handle);
            rez.serialize(op_id);
            rez.serialize(op_ctx_index);
            rez.serialize(index);
            rez.serialize(term_event);
            rez.serialize(collect_event);
            rez.serialize(local_collective_arrivals);
            rez.serialize(ready_event);
            rez.serialize(applied_event);
            trace_info.pack_trace_info(rez, applied_events);
          }
          // Add a remote valid reference that will be removed by 
          // the receiver once the changes have been applied
          WrapperReferenceMutator mutator(applied_events);
          add_base_valid_ref(REMOTE_DID_REF, &mutator);
          runtime->send_view_register_user(logical_owner, rez);
          applied_events.insert(applied_event);
        }
#ifdef ENABLE_VIEW_REPLICATION
        // If we have any local fields then we also need to update
        // them here too since the owner isn't going to send us any
        // updates itself, Do this after sending the message to make
        // sure that we see a sound set of local fields
        AutoLock r_lock(replicated_lock);
        // Only need to add it if it's still replicated
        const FieldMask local_mask = user_mask & replicated_fields;
        if (!!local_mask)
        {
          // See if we need to make the current users data structure
          if (current_users == NULL)
          {
            // Prevent races between multiple added users at the same time
            AutoLock v_lock(view_lock);
            // See if we lost the race
            if (current_users == NULL)
            {
              current_users = 
               new ExprView(context, manager, this, manager->instance_domain);
              current_users->add_reference();
            }
          }
          // Add our local user
          add_internal_task_user(usage, user_expr, local_mask, term_event,
                       collect_event, op_id, index, trace_info.recording);
          // Increment the number of remote added users
          remote_added_users++;
        }
        // If we have outstanding requests to be made a replicated
        // copy then we need to buffer this user so it can be applied
        // later once we actually do get the update from the owner
        // This only applies to updates from the local node though since
        // any remote updates will be sent to us again by the owner
        if ((repl_ptr.replicated_requests != NULL) && (source == local_space))
        {
#ifdef DEBUG_LEGION
          assert(!repl_ptr.replicated_requests->empty());
#endif
          FieldMask buffer_mask;
          for (LegionMap<RtUserEvent,FieldMask>::const_iterator
                it = repl_ptr.replicated_requests->begin();
                it != repl_ptr.replicated_requests->end(); it++)
          {
            const FieldMask overlap = user_mask & it->second;
            if (!overlap)
              continue;
#ifdef DEBUG_LEGION
            assert(overlap * buffer_mask);
#endif
            buffer_mask |= overlap;
            // This user isn't fully applied until the request comes
            // back to make this view valid and the user gets applied
            applied_events.insert(it->first);
          }
          if (!!buffer_mask)
          {
            // Protected by exclusive replicated lock
            if (remote_pending_users == NULL)
              remote_pending_users = new std::list<RemotePendingUser*>();
            remote_pending_users->push_back(
                new PendingTaskUser(usage, buffer_mask, user_expr, op_id,
                                    index, term_event, collect_event));
          }
        }
        if (remote_added_users >= user_cache_timeout)
          update_remote_replication_state(applied_events);
#endif // ENABLE_VIEW_REPLICATION
        return ready_event;
      }
      else
      {
#ifdef ENABLE_VIEW_REPLICATION
        // We need to hold a read-only copy of the replicated lock when
        // doing this in order to make sure it's atomic with any 
        // replication requests that arrive
        AutoLock r_lock(replicated_lock,1,false/*exclusive*/);
        // Send updates to any remote copies to get them in flight
        if (repl_ptr.replicated_copies != NULL)
        {
#ifdef DEBUG_LEGION
          assert(!repl_ptr.replicated_copies->empty());
#endif
          const FieldMask repl_mask = replicated_fields & user_mask;
          if (!!repl_mask)
          {
            for (LegionMap<AddressSpaceID,FieldMask>::const_iterator
                  it = repl_ptr.replicated_copies->begin(); 
                  it != repl_ptr.replicated_copies->end(); it++)
            {
              if (it->first == source)
                continue;
              const FieldMask overlap = it->second & repl_mask;
              if (!overlap)
                continue;
              // Send the update to the remote node
              RtUserEvent applied_event = Runtime::create_rt_user_event();
              Serializer rez;
              {
                RezCheck z(rez);
                rez.serialize(did);
                rez.serialize(target->did);
                rez.serialize(usage);
                rez.serialize(overlap);
                rez.serialize(user_expr->handle);
                rez.serialize(op_id);
                rez.serialize(op_ctx_index);
                rez.serialize(index);
                rez.serialize(term_event);
                rez.serialize(collect_event);
                rez.serialize(local_collective_arrivals);
                rez.serialize(ApUserEvent::NO_AP_USER_EVENT);
                rez.serialize(applied_event);
                trace_info.pack_trace_info(rez, applied_events);
              }
              runtime->send_view_register_user(it->first, rez);
              applied_events.insert(applied_event);
            }
          }
        }
#endif // ENABLE_VIEW_REPLICATION
        // Now we can do our local analysis
        std::set<ApEvent> wait_on_events;
        ApEvent start_use_event = manager->get_use_event(term_event);
        if (start_use_event.exists())
          wait_on_events.insert(start_use_event);
        // Find the preconditions
        const bool user_dominates = 
          (user_expr->expr_id == current_users->view_expr->expr_id) ||
          (user_expr->get_volume() == current_users->get_view_volume());
        {
          // Traversing the tree so need the expr_view lock
          AutoLock e_lock(expr_lock,1,false/*exclusive*/);
          current_users->find_user_preconditions(usage, user_expr, 
                            user_dominates, user_mask, term_event, 
                            op_id, index, wait_on_events, trace_info.recording);
        }
        // Add our local user
        add_internal_task_user(usage, user_expr, user_mask, term_event, 
                               collect_event, op_id,index,trace_info.recording);
        // At this point tasks shouldn't be allowed to wait on themselves
#ifdef DEBUG_LEGION
        if (term_event.exists())
          assert(wait_on_events.find(term_event) == wait_on_events.end());
#endif
        // Return the merge of the events
        if (!wait_on_events.empty())
          return Runtime::merge_events(&trace_info, wait_on_events);
        else
          return ApEvent::NO_AP_EVENT;
      }
    }

    //--------------------------------------------------------------------------
    ApEvent MaterializedView::find_copy_preconditions(bool reading,
                                            ReductionOpID redop,
                                            const FieldMask &copy_mask,
                                            IndexSpaceExpression *copy_expr,
                                            UniqueID op_id, unsigned index,
                                            std::set<RtEvent> &applied_events,
                                            const PhysicalTraceInfo &trace_info)
    //--------------------------------------------------------------------------
    {
      if (!is_logical_owner())
      {
        // Check to see if there are any replicated fields here which we
        // can handle locally so we don't have to send a message to the owner
        ApEvent result_event;
#ifdef ENABLE_VIEW_REPLICATION
        FieldMask new_remote_fields;
#endif
        FieldMask request_mask(copy_mask);
#ifdef ENABLE_VIEW_REPLICATION
        // See if we can handle this now while all the fields are local
        {
          AutoLock r_lock(replicated_lock,1,false/*exclusive*/);
          if (!!replicated_fields)
          {
            request_mask -= replicated_fields;
            if (!request_mask)
            {
              // All of our fields are local here so we can do the
              // analysis now without waiting for anything
              // We do this while holding the read-only lock on
              // replication to prevent invalidations of the
              // replication state while we're doing this analysis
#ifdef DEBUG_LEGION
              assert(current_users != NULL);
#endif
              std::set<ApEvent> preconditions;
              ApEvent start_use_event = manager->get_use_event();
              if (start_use_event.exists())
                preconditions.insert(start_use_event);
              const RegionUsage usage(reading ? LEGION_READ_ONLY : (redop > 0) ?
                  LEGION_REDUCE : LEGION_READ_WRITE, LEGION_EXCLUSIVE, redop);
              const bool copy_dominates = 
                (copy_expr->expr_id == current_users->view_expr->expr_id) ||
                (copy_expr->get_volume() == current_users->get_view_volume());
              {
                // Need a read-only copy of the expr_view lock to 
                // traverse the tree
                AutoLock e_lock(expr_lock,1,false/*exclusive*/);
                current_users->find_copy_preconditions(usage, copy_expr, 
                                       copy_dominates, copy_mask, op_id, 
                                       index, preconditions,
                                       trace_info.recording);
              }
              if (!preconditions.empty())
                result_event = Runtime::merge_events(&trace_info,preconditions);
              // See if there are any new fields we need to record
              // as having been used for copy precondition testing
              // We'll have to update them later with the lock in
              // exclusive mode, this is technically unsafe, but in
              // the worst case it will just invalidate the cache
              // and we'll have to make it valid again later
              new_remote_fields = copy_mask - remote_copy_pre_fields;
            }
          }
        }
        if (!!request_mask)
#endif // ENABLE_VIEW_REPLICATION
        {
          // All the fields are not local, first send the request to 
          // the owner to do the analysis since we're going to need 
          // to do that anyway, then issue any request for replicated
          // fields to be moved to this node and record it as a 
          // precondition for the mapping
          ApUserEvent ready_event = Runtime::create_ap_user_event(&trace_info);
          RtUserEvent applied = Runtime::create_rt_user_event();
          Serializer rez;
          {
            RezCheck z(rez);
            rez.serialize(did);
            rez.serialize<bool>(reading);
            rez.serialize(redop);
            rez.serialize(copy_mask);
            copy_expr->pack_expression(rez, logical_owner);
            rez.serialize(op_id);
            rez.serialize(index);
            rez.serialize(ready_event);
            rez.serialize(applied);
            trace_info.pack_trace_info(rez, applied_events);
          }
          runtime->send_view_find_copy_preconditions_request(logical_owner,rez);
          applied_events.insert(applied);
          result_event = ready_event;
#ifdef ENABLE_VIEW_REPLICATION
#ifndef DISABLE_VIEW_REPLICATION
          // Need the lock for this next part
          AutoLock r_lock(replicated_lock);
          // Record these fields as being sampled
          remote_copy_pre_fields |= (new_remote_fields & replicated_fields);
          // Recompute this to make sure we didn't lose any races
          request_mask = copy_mask - replicated_fields;
          if (!!request_mask && (repl_ptr.replicated_requests != NULL))
          {
            for (LegionMap<RtUserEvent,FieldMask>::const_iterator it = 
                  repl_ptr.replicated_requests->begin(); it !=
                  repl_ptr.replicated_requests->end(); it++)
            {
              request_mask -= it->second;
              if (!request_mask)
                break;
            }
          }
          if (!!request_mask)
          {
            // Send the request to the owner to make these replicated fields
            const RtUserEvent request_event = Runtime::create_rt_user_event();
            Serializer rez2;
            {
              RezCheck z2(rez2);
              rez2.serialize(did);
              rez2.serialize(request_mask);
              rez2.serialize(request_event);
            }
            runtime->send_view_replication_request(logical_owner, rez2);
            if (repl_ptr.replicated_requests == NULL)
              repl_ptr.replicated_requests =
                new LegionMap<RtUserEvent,FieldMask>();
            (*repl_ptr.replicated_requests)[request_event] = request_mask;
            // Make sure this is done before things are considered "applied"
            // in order to prevent dangling requests
            aggregator.record_reference_mutation_effect(request_event);
          }
#endif
#endif
        }
#ifdef ENABLE_VIEW_REPLICATION
        else if (!!new_remote_fields)
        {
          AutoLock r_lock(replicated_lock);
          // Record any new fields which are still replicated
          remote_copy_pre_fields |= (new_remote_fields & replicated_fields);
          // Then fall through like normal
        }
#endif 
        return result_event;
      }
      else
      {
        // In the case where we're the owner we can just handle
        // this without needing to do anything
        std::set<ApEvent> preconditions;
        const ApEvent start_use_event = manager->get_use_event();
        if (start_use_event.exists())
          preconditions.insert(start_use_event);
        const RegionUsage usage(reading ? LEGION_READ_ONLY : (redop > 0) ?
            LEGION_REDUCE : LEGION_READ_WRITE, LEGION_EXCLUSIVE, redop);
        const bool copy_dominates = 
          (copy_expr->expr_id == current_users->view_expr->expr_id) ||
          (copy_expr->get_volume() == current_users->get_view_volume());
        {
          // Need a read-only copy of the expr_lock to traverse the tree
          AutoLock e_lock(expr_lock,1,false/*exclusive*/);
          current_users->find_copy_preconditions(usage,copy_expr,copy_dominates,
                  copy_mask, op_id, index, preconditions, trace_info.recording);
        }
        if (preconditions.empty())
          return ApEvent::NO_AP_EVENT;
        return Runtime::merge_events(&trace_info, preconditions);
      }
    }

    //--------------------------------------------------------------------------
    void MaterializedView::add_copy_user(bool reading, ReductionOpID redop,
                                         ApEvent term_event,
                                         RtEvent collect_event,
                                         const FieldMask &copy_mask,
                                         IndexSpaceExpression *copy_expr,
                                         UniqueID op_id, unsigned index,
                                         std::set<RtEvent> &applied_events,
                                         const bool trace_recording,
                                         const AddressSpaceID source)
    //--------------------------------------------------------------------------
    {
      if (!is_logical_owner())
      {
        // Check to see if this update came from some place other than the
        // source in which case we need to send it back to the source
        if (source != logical_owner)
        {
          RtUserEvent applied_event = Runtime::create_rt_user_event();
          Serializer rez;
          {
            RezCheck z(rez);
            rez.serialize(did);
            rez.serialize<bool>(reading);
            rez.serialize(redop);
            rez.serialize(term_event);
            rez.serialize(collect_event);
            rez.serialize(copy_mask);
            copy_expr->pack_expression(rez, logical_owner);
            rez.serialize(op_id);
            rez.serialize(index);
            rez.serialize(applied_event);
            rez.serialize<bool>(trace_recording);
          }
          // Add a remote valid reference that will be removed by 
          // the receiver once the changes have been applied
          WrapperReferenceMutator mutator(applied_events);
          add_base_valid_ref(REMOTE_DID_REF, &mutator);
          runtime->send_view_add_copy_user(logical_owner, rez);
          applied_events.insert(applied_event);
        }
#ifdef ENABLE_VIEW_REPLICATION
        AutoLock r_lock(replicated_lock);
        // Only need to add it if it's still replicated
        const FieldMask local_mask = copy_mask & replicated_fields;
        // If we have local fields to handle do that here
        if (!!local_mask)
        {
          // See if we need to make the current users data structure
          if (current_users == NULL)
          {
            // Prevent races between multiple added users at the same time
            AutoLock v_lock(view_lock);
            // See if we lost the race
            if (current_users == NULL)
            {
              current_users = 
               new ExprView(context, manager, this, manager->instance_domain);
              current_users->add_reference();
            }
          }
          const RegionUsage usage(reading ? LEGION_READ_ONLY: (redop > 0) ? 
              LEGION_REDUCE : LEGION_READ_WRITE, LEGION_EXCLUSIVE, redop);
          add_internal_copy_user(usage, copy_expr, local_mask, term_event, 
                                 collect_event, op_id, index, trace_recording);
          // Increment the remote added users count
          remote_added_users++;
        }
        // If we have pending replicated requests that overlap with this
        // user then we need to record this as a pending user to be applied
        // once we receive the update from the owner node
        // This only applies to updates from the local node though since
        // any remote updates will be sent to us again by the owner
        if ((repl_ptr.replicated_requests != NULL) && (source == local_space))
        {
#ifdef DEBUG_LEGION
          assert(!repl_ptr.replicated_requests->empty());
#endif
          FieldMask buffer_mask;
          for (LegionMap<RtUserEvent,FieldMask>::const_iterator
                it = repl_ptr.replicated_requests->begin();
                it != repl_ptr.replicated_requests->end(); it++)
          {
            const FieldMask overlap = copy_mask & it->second;
            if (!overlap)
              continue;
#ifdef DEBUG_LEGION
            assert(overlap * buffer_mask);
#endif
            buffer_mask |= overlap;
            // This user isn't fully applied until the request comes
            // back to make this view valid and the user gets applied
            applied_events.insert(it->first);
          }
          if (!!buffer_mask)
          {
            // Protected by exclusive replicated lock
            if (remote_pending_users == NULL)
              remote_pending_users = new std::list<RemotePendingUser*>();
            remote_pending_users->push_back(
                new PendingCopyUser(reading, buffer_mask, copy_expr, op_id,
                                    index, term_event, collect_event));
          }
        }
        if (remote_added_users >= user_cache_timeout)
          update_remote_replication_state(applied_events);
#endif // ENABLE_VIEW_REPLICATION
      }
      else
      {
#ifdef ENABLE_VIEW_REPLICATION
        // We need to hold this lock in read-only mode to properly
        // synchronize this with any replication requests that arrive
        AutoLock r_lock(replicated_lock,1,false/*exclusive*/);
        // Send updates to any remote copies to get them in flight
        if (repl_ptr.replicated_copies != NULL)
        {
#ifdef DEBUG_LEGION
          assert(!repl_ptr.replicated_copies->empty());
#endif
          const FieldMask repl_mask = replicated_fields & copy_mask;
          if (!!repl_mask)
          {
            for (LegionMap<AddressSpaceID,FieldMask>::const_iterator
                  it = repl_ptr.replicated_copies->begin(); 
                  it != repl_ptr.replicated_copies->end(); it++)
            {
              if (it->first == source)
                continue;
              const FieldMask overlap = it->second & repl_mask;
              if (!overlap)
                continue;
              RtUserEvent applied_event = Runtime::create_rt_user_event();
              Serializer rez;
              {
                RezCheck z(rez);
                rez.serialize(did);
                rez.serialize<bool>(reading);
                rez.serialize(redop);
                rez.serialize(term_event);
                rez.serialize(collect_event);
                rez.serialize(copy_mask);
                copy_expr->pack_expression(rez, it->first);
                rez.serialize(op_id);
                rez.serialize(index);
                rez.serialize(applied_event);
                rez.serialize<bool>(trace_recording);
              }
              runtime->send_view_add_copy_user(it->first, rez);
              applied_events.insert(applied_event);
            }
          }
        }
#endif
        // Now we can do our local analysis
        const RegionUsage usage(reading ? LEGION_READ_ONLY : (redop > 0) ?
            LEGION_REDUCE : LEGION_READ_WRITE, LEGION_EXCLUSIVE, redop);
        add_internal_copy_user(usage, copy_expr, copy_mask, term_event, 
                               collect_event, op_id, index, trace_recording);
      }
    }

    //--------------------------------------------------------------------------
    void MaterializedView::find_last_users(PhysicalManager *instance,
                                      std::set<ApEvent> &events,
                                      const RegionUsage &usage,
                                      const FieldMask &mask,
                                      IndexSpaceExpression *expr,
                                      std::vector<RtEvent> &ready_events) const
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(instance == manager);
#endif
      // Check to see if we're on the right node to perform this analysis
      if (logical_owner != local_space)
      {
        const RtUserEvent ready = Runtime::create_rt_user_event();
        Serializer rez;
        {
          RezCheck z(rez);
          rez.serialize(did);
          rez.serialize(manager->did);
          rez.serialize(&events);
          rez.serialize(usage);
          rez.serialize(mask);
          expr->pack_expression(rez, logical_owner);
          rez.serialize(ready);
        }
        runtime->send_view_find_last_users_request(logical_owner, rez);
        ready_events.push_back(ready);
      }
      else
      {
        const bool expr_dominates = 
          (expr->expr_id == current_users->view_expr->expr_id) ||
          (expr->get_volume() == current_users->get_view_volume());
        {
          // Need a read-only copy of the expr_lock to traverse the tree
          AutoLock e_lock(expr_lock,1,false/*exclusive*/);
          current_users->find_last_users(usage, expr, expr_dominates,
                                         mask, events);
        }
      }
    }

#ifdef ENABLE_VIEW_REPLICATION
    //--------------------------------------------------------------------------
    void MaterializedView::process_replication_request(AddressSpaceID source,
                                                  const FieldMask &request_mask,
                                                  RtUserEvent done_event)
    //--------------------------------------------------------------------------
    {
      // Atomically we need to package up the response and send it back
      AutoLock r_lock(replicated_lock); 
      if (repl_ptr.replicated_copies == NULL)
        repl_ptr.replicated_copies = 
          new LegionMap<AddressSpaceID,FieldMask>();
      LegionMap<AddressSpaceID,FieldMask>::iterator finder = 
        repl_ptr.replicated_copies->find(source);
      if (finder != repl_ptr.replicated_copies->end())
      {
#ifdef DEBUG_LEGION
        assert(finder->second * request_mask);
#endif
        finder->second |= request_mask; 
      }
      else
        (*repl_ptr.replicated_copies)[source] = request_mask;
      // Update the summary as well
      replicated_fields |= request_mask;
      Serializer rez;
      {
        RezCheck z(rez);
        rez.serialize(did);
        rez.serialize(done_event);
        std::map<PhysicalUser*,unsigned> indexes;
        // Make sure no one else is mutating the state of the tree
        // while we are doing the packing
        AutoLock e_lock(expr_lock,1,false/*exclusive*/);
        current_users->pack_replication(rez, indexes, request_mask, source);
      }
      runtime->send_view_replication_response(source, rez);
    }

    //--------------------------------------------------------------------------
    void MaterializedView::process_replication_response(RtUserEvent done,
                                                        Deserializer &derez)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(!is_logical_owner());
#endif
      AutoLock r_lock(replicated_lock);
      {
        // Take the view lock so we can modify the cache as well
        // as part of our unpacking
        AutoLock v_lock(view_lock);
        if (current_users == NULL)
        {
          current_users = 
            new ExprView(context, manager, this, manager->instance_domain);
          current_users->add_reference();
        }
        // We need to hold the expr lock here since we might have to 
        // make ExprViews and we need this to be atomic with other
        // operations that might also try to mutate the tree 
        AutoLock e_lock(expr_lock);
        std::vector<PhysicalUser*> users;
        // The source is always from the logical owner space
        current_users->unpack_replication(derez, current_users, 
                                          logical_owner, expr_cache, users);
        // Remove references from all our users
        for (unsigned idx = 0; idx < users.size(); idx++)
          if (users[idx]->remove_reference())
            delete users[idx]; 
      }
#ifdef DEBUG_LEGION
      assert(repl_ptr.replicated_requests != NULL);
#endif
      LegionMap<RtUserEvent,FieldMask>::iterator finder = 
        repl_ptr.replicated_requests->find(done);
#ifdef DEBUG_LEGION
      assert(finder != repl_ptr.replicated_requests->end());
#endif
      // Go through and apply any pending remote users we've recorded 
      if (remote_pending_users != NULL)
      {
        for (std::list<RemotePendingUser*>::iterator it = 
              remote_pending_users->begin(); it != 
              remote_pending_users->end(); /*nothing*/)
        {
          if ((*it)->apply(this, finder->second))
          {
            delete (*it);
            it = remote_pending_users->erase(it);
          }
          else
            it++;
        }
        if (remote_pending_users->empty())
        {
          delete remote_pending_users;
          remote_pending_users = NULL;
        }
      }
      // Record that these fields are now replicated
      replicated_fields |= finder->second;
      repl_ptr.replicated_requests->erase(finder);
      if (repl_ptr.replicated_requests->empty())
      {
        delete repl_ptr.replicated_requests;
        repl_ptr.replicated_requests = NULL;
      }
    }

    //--------------------------------------------------------------------------
    void MaterializedView::process_replication_removal(AddressSpaceID source,
                                                  const FieldMask &removal_mask)
    //--------------------------------------------------------------------------
    {
      AutoLock r_lock(replicated_lock);
#ifdef DEBUG_LEGION
      assert(is_logical_owner());
      assert(repl_ptr.replicated_copies != NULL);
#endif
      LegionMap<AddressSpaceID,FieldMask>::iterator finder = 
        repl_ptr.replicated_copies->find(source);
#ifdef DEBUG_LEGION
      assert(finder != repl_ptr.replicated_copies->end());
      // We should know about all the fields being removed
      assert(!(removal_mask - finder->second));
#endif
      finder->second -= removal_mask;
      if (!finder->second)
      {
        repl_ptr.replicated_copies->erase(finder);
        if (repl_ptr.replicated_copies->empty())
        {
          delete repl_ptr.replicated_copies;
          repl_ptr.replicated_copies = NULL;
          replicated_fields.clear();
          return;
        }
        // Otherwise fall through and rebuild the replicated fields
      }
      // Rebuild the replicated fields so they are precise
      if (repl_ptr.replicated_copies->size() > 1)
      {
        replicated_fields.clear();
        for (LegionMap<AddressSpaceID,FieldMask>::const_iterator it =
              repl_ptr.replicated_copies->begin(); it !=
              repl_ptr.replicated_copies->end(); it++)
          replicated_fields |= finder->second;
      }
      else
        replicated_fields = repl_ptr.replicated_copies->begin()->second;
    }
#endif // ENABLE_VIEW_REPLICATION
 
    //--------------------------------------------------------------------------
    void MaterializedView::add_internal_task_user(const RegionUsage &usage,
                                            IndexSpaceExpression *user_expr,
                                            const FieldMask &user_mask,
                                            ApEvent term_event, 
                                            RtEvent collect_event, 
                                            UniqueID op_id,
                                            const unsigned index,
                                            const bool trace_recording)
    //--------------------------------------------------------------------------
    {
#ifdef ENABLE_VIEW_REPLICATION
      PhysicalUser *user = new PhysicalUser(usage, user_expr, op_id, index, 
                             collect_event, false/*copy user*/, true/*covers*/);
#else
      PhysicalUser *user = new PhysicalUser(usage, user_expr, op_id, index, 
                                            false/*copy user*/, true/*covers*/);
#endif
      // Hold a reference to this in case it finishes before we're done
      // with the analysis and its get pruned/deleted
      user->add_reference();
      ExprView *target_view = NULL;
      bool has_target_view = false;
      // Handle an easy case first, if the user_expr is the same as the 
      // view_expr for the root then this is easy
      bool update_count = true;
      bool update_cache = false;
      if (user_expr != current_users->view_expr)
      {
        // Hard case where we will have subviews
        AutoLock v_lock(view_lock,1,false/*exclusive*/);
        // See if we can find the entry in the cache and it's valid 
        // for all of our fields
        LegionMap<IndexSpaceExprID,ExprView*>::const_iterator
          finder = expr_cache.find(user_expr->expr_id);
        if (finder != expr_cache.end())
        {
          target_view = finder->second;
          AutoLock e_lock(expr_lock,1,false/*exclusive*/);
          if (finder->second->invalid_fields * user_mask)
            has_target_view = true;
        }
        else
          update_cache = true;
        // increment the number of outstanding additions
        outstanding_additions.fetch_add(1);
      }
      else // This is just going to add at the top so never needs to wait
      {
        target_view = current_users;
        update_count = false;
        has_target_view = true;
      }
      if (!has_target_view)
      {
        // This could change the shape of the view tree so we need
        // exclusive privilege son the expr lock to serialize it
        // with everything else traversing the tree
        AutoLock e_lock(expr_lock);
        // If we don't have a target view see if there is a 
        // congruent one already in the tree
        if (target_view == NULL)
        {
          target_view = current_users->find_congruent_view(user_expr);
          if (target_view == NULL)
            target_view = new ExprView(context, manager, this, user_expr);
        }
        if (target_view != current_users)
        {
          // Now see if we need to insert it
          FieldMask insert_mask = user_mask & target_view->invalid_fields;
          if (!!insert_mask)
          {
            // Remove these fields from being invalid before we
            // destroy the insert mask
            target_view->invalid_fields -= insert_mask;
            // Do the insertion into the tree
            current_users->insert_subview(target_view, insert_mask);
          }
        }
      }
      // Now we know the target view and it's valid for all fields
      // so we can add it to the expr view
      target_view->add_current_user(user, term_event, collect_event,
                                    user_mask, trace_recording);
      if (user->remove_reference())
        delete user;
      AutoLock v_lock(view_lock);
      if (update_count)
      {
#ifdef DEBUG_LEGION
        assert(outstanding_additions.load() > 0);
#endif
        if ((outstanding_additions.fetch_sub(1) == 1) && clean_waiting.exists())
        {
          // Wake up the clean waiter
          Runtime::trigger_event(clean_waiting);
          clean_waiting = RtUserEvent::NO_RT_USER_EVENT;
        }
      }
      if (!update_cache)
      {
        // Update the timeout and see if we need to clear the cache
        if (!expr_cache.empty())
        {
          expr_cache_uses++;
          // Check for equality guarantees only one thread in here at a time
          if (expr_cache_uses == user_cache_timeout)
          {
            // Wait until there are are no more outstanding additions
            while (outstanding_additions.load() > 0)
            {
#ifdef DEBUG_LEGION
              assert(!clean_waiting.exists());
#endif
              clean_waiting = Runtime::create_rt_user_event();
              const RtEvent wait_on = clean_waiting;
              v_lock.release();
              wait_on.wait();
              v_lock.reacquire();
            }
            clean_cache<true/*need expr lock*/>();
          }
        }
      }
      else
        expr_cache[user_expr->expr_id] = target_view;
    }

    //--------------------------------------------------------------------------
    void MaterializedView::add_internal_copy_user(const RegionUsage &usage,
                                            IndexSpaceExpression *user_expr,
                                            const FieldMask &user_mask,
                                            ApEvent term_event, 
                                            RtEvent collect_event, 
                                            UniqueID op_id,
                                            const unsigned index,
                                            const bool trace_recording)
    //--------------------------------------------------------------------------
    { 
      // First we're going to check to see if we can add this directly to 
      // an existing ExprView with the same expresssion in which case
      // we'll be able to mark this user as being precise
      ExprView *target_view = NULL;
      bool has_target_view = false;
      // Handle an easy case first, if the user_expr is the same as the 
      // view_expr for the root then this is easy
      bool update_count = false;
      bool update_cache = false;
      if (user_expr != current_users->view_expr)
      {
        // Hard case where we will have subviews
        AutoLock v_lock(view_lock,1,false/*exclusive*/);
        // See if we can find the entry in the cache and it's valid 
        // for all of our fields
        LegionMap<IndexSpaceExprID,ExprView*>::const_iterator
          finder = expr_cache.find(user_expr->expr_id);
        if (finder != expr_cache.end())
        {
          target_view = finder->second;
          AutoLock e_lock(expr_lock,1,false/*exclusive*/);
          if (finder->second->invalid_fields * user_mask)
            has_target_view = true;
        }
        // increment the number of outstanding additions
        outstanding_additions.fetch_add(1);
        update_count = true;
      }
      else // This is just going to add at the top so never needs to wait
      {
        target_view = current_users;
        has_target_view = true;
      }
      if (!has_target_view)
      {
        // Do a quick test to see if we can find a target view
        AutoLock e_lock(expr_lock);
        // If we haven't found it yet, see if we can find it
        if (target_view == NULL)
        {
          target_view = current_users->find_congruent_view(user_expr);
          if (target_view != NULL)
            update_cache = true;
        }
        // Don't make it though if we don't already have it
        if (target_view != NULL)
        {
          // No need to insert this if it's the root
          if (target_view != current_users)
          {
            FieldMask insert_mask = target_view->invalid_fields & user_mask;
            if (!!insert_mask)
            {
              target_view->invalid_fields -= insert_mask;
              current_users->insert_subview(target_view, insert_mask);
            }
          }
          has_target_view = true;
        }
      }
      if (has_target_view)
      {
        // If we have a target view, then we know we cover it because
        // the expressions match directly
#ifdef ENABLE_VIEW_REPLICATION
        PhysicalUser *user = new PhysicalUser(usage, user_expr, op_id, index, 
                               collect_event, true/*copy user*/,true/*covers*/);
#else
        PhysicalUser *user = new PhysicalUser(usage, user_expr, op_id, index, 
                                              true/*copy user*/,true/*covers*/);
#endif
        // Hold a reference to this in case it finishes before we're done
        // with the analysis and its get pruned/deleted
        user->add_reference();
        // We already know the view so we can just add the user directly
        // there and then do any updates that we need to
        target_view->add_current_user(user, term_event, collect_event, 
                                      user_mask, trace_recording);
        if (user->remove_reference())
          delete user;
        if (update_count || update_cache)
        {
          AutoLock v_lock(view_lock);
          if (update_cache)
            expr_cache[user_expr->expr_id] = target_view;
          if (update_count)
          {
#ifdef DEBUG_LEGION
            assert(outstanding_additions.load() > 0);
#endif
            if ((outstanding_additions.fetch_sub(1) == 1) && 
                clean_waiting.exists())
            {
              // Wake up the clean waiter
              Runtime::trigger_event(clean_waiting);
              clean_waiting = RtUserEvent::NO_RT_USER_EVENT;
            }
          }
        }
      }
      else
      {
#ifdef DEBUG_LEGION
        assert(update_count); // this should always be true
        assert(!update_cache); // this should always be false
#endif
        // This is a case where we don't know where to add the copy user
        // so we need to traverse down and find one, 
        {
          // We're traversing the view tree but not modifying it so 
          // we need a read-only copy of the expr_lock
          AutoLock e_lock(expr_lock,1,false/*exclusive*/);
          current_users->add_partial_user(usage, op_id, index,
                                          user_mask, term_event, 
                                          collect_event, user_expr, 
                                          user_expr->get_volume(), 
                                          trace_recording);
        }
        AutoLock v_lock(view_lock);
#ifdef DEBUG_LEGION
        assert(outstanding_additions.load() > 0);
#endif
        if ((outstanding_additions.fetch_sub(1) == 1) && clean_waiting.exists())
        {
          // Wake up the clean waiter
          Runtime::trigger_event(clean_waiting);
          clean_waiting = RtUserEvent::NO_RT_USER_EVENT;
        }
      } 
    }

    //--------------------------------------------------------------------------
    template<bool NEED_EXPR_LOCK>
    void MaterializedView::clean_cache(void)
    //--------------------------------------------------------------------------
    {
      // Clear the cache
      expr_cache.clear();
      // Reset the cache use counter
      expr_cache_uses = 0;
      // Anytime we clean the cache, we also traverse the 
      // view tree and see if there are any views we can 
      // remove because they no longer have live users
      FieldMask dummy_mask; 
      FieldMaskSet<ExprView> clean_set;
      if (NEED_EXPR_LOCK)
      {
        // Take the lock in exclusive mode since we might be modifying the tree
        AutoLock e_lock(expr_lock);
        current_users->clean_views(dummy_mask, clean_set);
        // We can safely repopulate the cache with any view expressions which
        // are still valid, remove all references for views in the clean set 
        for (FieldMaskSet<ExprView>::const_iterator it = 
              clean_set.begin(); it != clean_set.end(); it++)
        {
          if (!!(~(it->first->invalid_fields)))
            expr_cache[it->first->view_expr->expr_id] = it->first;
          if (it->first->remove_reference())
            delete it->first;
        }
      }
      else
      {
        // Same as above, but without needing to acquire the lock
        // because the caller promised that they already have it
        current_users->clean_views(dummy_mask, clean_set);
        // We can safely repopulate the cache with any view expressions which
        // are still valid, remove all references for views in the clean set 
        for (FieldMaskSet<ExprView>::const_iterator it = 
              clean_set.begin(); it != clean_set.end(); it++)
        {
          if (!!(~(it->first->invalid_fields)))
            expr_cache[it->first->view_expr->expr_id] = it->first;
          if (it->first->remove_reference())
            delete it->first;
        }
      }
    }

#ifdef ENABLE_VIEW_REPLICATION
    //--------------------------------------------------------------------------
    void MaterializedView::update_remote_replication_state(
                                              std::set<RtEvent> &applied_events)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(!is_logical_owner());
      assert(!!replicated_fields);
      assert(current_users != NULL);
      assert(remote_added_users >= user_cache_timeout);
#endif
      // We can reset the counter now
      remote_added_users = 0;
      // See what fields haven't been sampled recently and therefore
      // we should stop maintaining as remote duplicates
      const FieldMask deactivate_mask = 
        replicated_fields - remote_copy_pre_fields; 
      // We can clear this now for the next epoch
      remote_copy_pre_fields.clear();
      // If we have any outstanding requests though keep those
      if (repl_ptr.replicated_requests != NULL)
      {
        for (LegionMap<RtUserEvent,FieldMask>::const_iterator it = 
              repl_ptr.replicated_requests->begin(); it !=
              repl_ptr.replicated_requests->end(); it++)
        {
#ifdef DEBUG_LEGION
          assert(it->second * deactivate_mask);
#endif
          remote_copy_pre_fields |= it->second;
        }
      }
      // If we don't have any fields to deactivate then we're done
      if (!deactivate_mask)
        return;
      // Send the message to do the deactivation on the owner node
      RtUserEvent done_event = Runtime::create_rt_user_event();
      Serializer rez;
      {
        RezCheck z(rez);
        rez.serialize(did);
        rez.serialize(deactivate_mask);
        rez.serialize(done_event);
      }
      runtime->send_view_replication_removal(logical_owner, rez);
      applied_events.insert(done_event);
      // Perform it locally
      {
        // Anytime we do a deactivate that can influence the valid
        // set of ExprView objects so we need to clean the cache
        AutoLock v_lock(view_lock);
#ifdef DEBUG_LEGION
        // There should be no outstanding_additions when we're here
        // because we're already protected by the replication lock
        assert(outstanding_additions.load() == 0);
#endif
        // Go through and remove any users for the deactivate mask
        // Need an exclusive copy of the expr_lock to do this
        AutoLock e_lock(expr_lock);
        current_users->deactivate_replication(deactivate_mask);
        // Then clean the cache since we likely invalidated some
        // things. This will also go through and remove any views
        // that no longer have any active users
        clean_cache<false/*need expr lock*/>();
      }
      // Record that these fields are no longer replicated 
      replicated_fields -= deactivate_mask;
    }
#endif // ENABLE_VIEW_REPLICATION

    //--------------------------------------------------------------------------
    void MaterializedView::send_view(AddressSpaceID target)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(is_owner());
#endif
      // Check to see if this is a replicated view, if the target
      // is in the replicated set, then there's nothing we need to do
      // We can just ignore this and the registration will be done later
      if ((collective_mapping != NULL) && collective_mapping->contains(target))
        return;
      Serializer rez;
      {
        RezCheck z(rez);
        rez.serialize(did);
        rez.serialize(manager->did);
        rez.serialize(owner_space);
        rez.serialize(logical_owner);
        rez.serialize(owner_context);
      }
      runtime->send_materialized_view(target, rez);
      update_remote_instances(target);
    } 

    //--------------------------------------------------------------------------
    /*static*/ void MaterializedView::handle_send_materialized_view(
                  Runtime *runtime, Deserializer &derez, AddressSpaceID source)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez); 
      DistributedID did;
      derez.deserialize(did);
      DistributedID manager_did;
      derez.deserialize(manager_did);
      AddressSpaceID owner_space;
      derez.deserialize(owner_space);
      AddressSpaceID logical_owner;
      derez.deserialize(logical_owner);
      UniqueID context_uid;
      derez.deserialize(context_uid);
      RtEvent man_ready;
      PhysicalManager *manager =
        runtime->find_or_request_instance_manager(manager_did, man_ready);
      if (man_ready.exists() && !man_ready.has_triggered())
      {
        // Defer this until the manager is ready
        DeferMaterializedViewArgs args(did, manager, owner_space,
                                       logical_owner, context_uid);
        runtime->issue_runtime_meta_task(args, 
            LG_LATENCY_RESPONSE_PRIORITY, man_ready);
      }
      else
        create_remote_view(runtime, did, manager, owner_space, 
                           logical_owner, context_uid); 
    }

    //--------------------------------------------------------------------------
    /*static*/ void MaterializedView::handle_defer_materialized_view(
                                             const void *args, Runtime *runtime)
    //--------------------------------------------------------------------------
    {
      const DeferMaterializedViewArgs *dargs = 
        (const DeferMaterializedViewArgs*)args; 
      create_remote_view(runtime, dargs->did, dargs->manager, 
          dargs->owner_space, dargs->logical_owner, dargs->context_uid);
    }

    //--------------------------------------------------------------------------
    /*static*/ void MaterializedView::create_remote_view(Runtime *runtime,
                            DistributedID did, PhysicalManager *manager,
                            AddressSpaceID owner_space,
                            AddressSpaceID logical_owner, UniqueID context_uid)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(manager->is_physical_manager());
#endif
      PhysicalManager *inst_manager = manager->as_physical_manager();
      void *location;
      MaterializedView *view = NULL;
      if (runtime->find_pending_collectable_location(did, location))
        view = new(location) MaterializedView(runtime->forest,
                                              did, owner_space, 
                                              logical_owner, inst_manager,
                                              context_uid,
                                              false/*register now*/);
      else
        view = new MaterializedView(runtime->forest, did, owner_space,
                                    logical_owner, inst_manager, 
                                    context_uid, false/*register now*/);
      // Register only after construction
      view->register_with_runtime();
    }

    /////////////////////////////////////////////////////////////
    // DeferredView 
    /////////////////////////////////////////////////////////////

    //--------------------------------------------------------------------------
    DeferredView::DeferredView(RegionTreeForest *ctx, DistributedID did,
                               AddressSpaceID owner_sp, bool register_now)
      : LogicalView(ctx, did, owner_sp, register_now, NULL/*no collective map*/)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    DeferredView::~DeferredView(void)
    //--------------------------------------------------------------------------
    {
    }

    /////////////////////////////////////////////////////////////
    // FillView 
    /////////////////////////////////////////////////////////////

    //--------------------------------------------------------------------------
    FillView::FillView(RegionTreeForest *ctx, DistributedID did,
                       AddressSpaceID owner_proc,
                       FillViewValue *val, bool register_now
#ifdef LEGION_SPY
                       , UniqueID op_uid
#endif
                       )
      : DeferredView(ctx, encode_fill_did(did), owner_proc, register_now), 
        value(val)
#ifdef LEGION_SPY
        , fill_op_uid(op_uid)
#endif
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(value != NULL);
#endif
      value->add_reference();
#ifdef LEGION_GC
      log_garbage.info("GC Fill View %lld %d", 
          LEGION_DISTRIBUTED_ID_FILTER(this->did), local_space);
#endif
    }

    //--------------------------------------------------------------------------
    FillView::FillView(const FillView &rhs)
      : DeferredView(NULL, 0, 0, false), value(NULL)
#ifdef LEGION_SPY
        , fill_op_uid(0)
#endif
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
    }
    
    //--------------------------------------------------------------------------
    FillView::~FillView(void)
    //--------------------------------------------------------------------------
    {
      if (value->remove_reference())
        delete value;
    }

    //--------------------------------------------------------------------------
    FillView& FillView::operator=(const FillView &rhs)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
      return *this;
    }

    //--------------------------------------------------------------------------
    void FillView::notify_active(ReferenceMutator *mutator)
    //--------------------------------------------------------------------------
    {
      if (!is_owner())
        send_remote_gc_increment(owner_space, mutator);
    }

    //--------------------------------------------------------------------------
    void FillView::notify_inactive(ReferenceMutator *mutator)
    //--------------------------------------------------------------------------
    {
      if (!is_owner())
        send_remote_gc_decrement(owner_space, mutator);
    }
    
    //--------------------------------------------------------------------------
    void FillView::notify_valid(ReferenceMutator *mutator)
    //--------------------------------------------------------------------------
    {
      // Nothing to do
    }

    //--------------------------------------------------------------------------
    void FillView::notify_invalid(ReferenceMutator *mutator)
    //--------------------------------------------------------------------------
    {
      // Nothing to do
    }

    //--------------------------------------------------------------------------
    void FillView::send_view(AddressSpaceID target)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(is_owner());
      assert(collective_mapping == NULL);
#endif
      Serializer rez;
      {
        RezCheck z(rez);
        rez.serialize(did);
        rez.serialize(owner_space);
        rez.serialize(value->value_size);
        rez.serialize(value->value, value->value_size);
#ifdef LEGION_SPY
        rez.serialize(fill_op_uid);
#endif
      }
      runtime->send_fill_view(target, rez);
      // We've now done the send so record it
      update_remote_instances(target);
    }

    //--------------------------------------------------------------------------
    void FillView::flatten(CopyFillAggregator &aggregator,
                         InstanceView *dst_view, const FieldMask &src_mask,
                         IndexSpaceExpression *expr, EquivalenceSet *tracing_eq, 
                         std::set<RtEvent> &applied, CopyAcrossHelper *helper)
    //--------------------------------------------------------------------------
    {
      aggregator.record_fill(dst_view, this, src_mask, expr, 
                             tracing_eq, applied, helper);
    }

    //--------------------------------------------------------------------------
    /*static*/ void FillView::handle_send_fill_view(Runtime *runtime,
                                     Deserializer &derez, AddressSpaceID source)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez);
      DistributedID did;
      derez.deserialize(did);
      AddressSpaceID owner_space;
      derez.deserialize(owner_space);
      size_t value_size;
      derez.deserialize(value_size);
      void *value = malloc(value_size);
      derez.deserialize(value, value_size);
#ifdef LEGION_SPY
      UniqueID op_uid;
      derez.deserialize(op_uid);
#endif
      
      FillView::FillViewValue *fill_value = 
                      new FillView::FillViewValue(value, value_size);
      void *location;
      FillView *view = NULL;
      if (runtime->find_pending_collectable_location(did, location))
        view = new(location) FillView(runtime->forest, did,
                                      owner_space, fill_value,
                                      false/*register now*/
#ifdef LEGION_SPY
                                      , op_uid
#endif
                                      );
      else
        view = new FillView(runtime->forest, did, owner_space,
                            fill_value, false/*register now*/
#ifdef LEGION_SPY
                            , op_uid
#endif
                            );
      view->register_with_runtime();
    }

    /////////////////////////////////////////////////////////////
    // PhiView 
    /////////////////////////////////////////////////////////////

    //--------------------------------------------------------------------------
    PhiView::PhiView(RegionTreeForest *ctx, DistributedID did, 
                     AddressSpaceID owner_space,
                     PredEvent tguard, PredEvent fguard, 
                     InnerContext *owner, bool register_now) 
      : DeferredView(ctx, encode_phi_did(did), owner_space, register_now),
        true_guard(tguard), false_guard(fguard), owner_context(owner)
    //--------------------------------------------------------------------------
    {
#ifdef LEGION_GC
      log_garbage.info("GC Phi View %lld %d", 
          LEGION_DISTRIBUTED_ID_FILTER(this->did), local_space);
#endif
    }

    //--------------------------------------------------------------------------
    PhiView::PhiView(const PhiView &rhs)
      : DeferredView(NULL, 0, 0, false),
        true_guard(PredEvent::NO_PRED_EVENT), 
        false_guard(PredEvent::NO_PRED_EVENT), owner_context(NULL)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
    }

    //--------------------------------------------------------------------------
    PhiView::~PhiView(void)
    //--------------------------------------------------------------------------
    {
      for (LegionMap<LogicalView*,FieldMask>::const_iterator it = 
            true_views.begin(); it != true_views.end(); it++)
      {
        if (it->first->remove_nested_resource_ref(did))
          delete it->first;
      }
      true_views.clear();
      for (LegionMap<LogicalView*,FieldMask>::const_iterator it =
            false_views.begin(); it != false_views.end(); it++)
      {
        if (it->first->remove_nested_resource_ref(did))
          delete it->first;
      }
      false_views.clear();
    }

    //--------------------------------------------------------------------------
    PhiView& PhiView::operator=(const PhiView &rhs)
    //--------------------------------------------------------------------------
    {
      // should never be called
      assert(false);
      return *this;
    }

    //--------------------------------------------------------------------------
    void PhiView::notify_active(ReferenceMutator *mutator)
    //--------------------------------------------------------------------------
    {
      if (!is_owner())
        send_remote_gc_increment(owner_space, mutator);
    }

    //--------------------------------------------------------------------------
    void PhiView::notify_inactive(ReferenceMutator *mutator)
    //--------------------------------------------------------------------------
    {
      if (!is_owner())
        send_remote_gc_decrement(owner_space, mutator);
    }

    //--------------------------------------------------------------------------
    void PhiView::notify_valid(ReferenceMutator *mutator)
    //--------------------------------------------------------------------------
    {
      for (LegionMap<LogicalView*,FieldMask>::const_iterator it =
            true_views.begin(); it != true_views.end(); it++)
        it->first->add_nested_valid_ref(did, mutator);
      for (LegionMap<LogicalView*,FieldMask>::const_iterator it = 
            false_views.begin(); it != false_views.end(); it++)
        it->first->add_nested_valid_ref(did, mutator);
    }

    //--------------------------------------------------------------------------
    void PhiView::notify_invalid(ReferenceMutator *mutator)
    //--------------------------------------------------------------------------
    {
      for (LegionMap<LogicalView*,FieldMask>::const_iterator it =
            true_views.begin(); it != true_views.end(); it++)
        it->first->remove_nested_valid_ref(did, mutator);
      for (LegionMap<LogicalView*,FieldMask>::const_iterator it =
            false_views.begin(); it != false_views.end(); it++)
        it->first->remove_nested_valid_ref(did, mutator);
    }

    //--------------------------------------------------------------------------
    void PhiView::record_true_view(LogicalView *view, const FieldMask &mask,
                                   ReferenceMutator *mutator)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(is_owner());
#endif
      LegionMap<LogicalView*,FieldMask>::iterator finder = 
        true_views.find(view);
      if (finder == true_views.end())
      {
        true_views[view] = mask;
        if (view->is_deferred_view())
        {
          // Deferred views need valid and gc references
          view->add_nested_gc_ref(did, mutator);
          view->add_nested_valid_ref(did, mutator);
        }
        else // Otherwise we just need the valid reference
          view->add_nested_resource_ref(did);
      }
      else
        finder->second |= mask;
    }

    //--------------------------------------------------------------------------
    void PhiView::record_false_view(LogicalView *view, const FieldMask &mask,
                                    ReferenceMutator *mutator)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(is_owner());
#endif
      LegionMap<LogicalView*,FieldMask>::iterator finder = 
        false_views.find(view);
      if (finder == false_views.end())
      {
        false_views[view] = mask;
        if (view->is_deferred_view())
        {
          // Deferred views need valid and gc references
          view->add_nested_gc_ref(did, mutator);
          view->add_nested_valid_ref(did, mutator);
        }
        else // Otherwise we just need the valid reference
          view->add_nested_resource_ref(did);
      }
      else
        finder->second |= mask;
    }

    //--------------------------------------------------------------------------
    void PhiView::pack_phi_view(Serializer &rez)
    //--------------------------------------------------------------------------
    {
      rez.serialize<size_t>(true_views.size());
      for (LegionMap<LogicalView*,FieldMask>::const_iterator it = 
            true_views.begin(); it != true_views.end(); it++)
      {
        rez.serialize(it->first->did);
        rez.serialize(it->second);
      }
      rez.serialize<size_t>(false_views.size());
      for (LegionMap<LogicalView*,FieldMask>::const_iterator it = 
            false_views.begin(); it != false_views.end(); it++)
      {
        rez.serialize(it->first->did);
        rez.serialize(it->second);
      }
    }

    //--------------------------------------------------------------------------
    void PhiView::unpack_phi_view(Deserializer &derez, 
                                  std::set<RtEvent> &preconditions)
    //--------------------------------------------------------------------------
    {
      size_t num_true_views;
      derez.deserialize(num_true_views);
      for (unsigned idx = 0; idx < num_true_views; idx++)
      {
        DistributedID view_did;
        derez.deserialize(view_did);
        RtEvent ready;
        LogicalView *view = static_cast<LogicalView*>(
            runtime->find_or_request_logical_view(view_did, ready));
        derez.deserialize(true_views[view]);
        if (ready.exists() && !ready.has_triggered())
          preconditions.insert(defer_add_reference(view, ready));
        else // Otherwise we can add the reference now
          view->add_nested_resource_ref(did);
      }
      size_t num_false_views;
      derez.deserialize(num_false_views);
      for (unsigned idx = 0; idx < num_false_views; idx++)
      {
        DistributedID view_did;
        derez.deserialize(view_did);
        RtEvent ready;
        LogicalView *view = static_cast<LogicalView*>(
            runtime->find_or_request_logical_view(view_did, ready));
        derez.deserialize(false_views[view]);
        if (ready.exists() && !ready.has_triggered())
          preconditions.insert(defer_add_reference(view, ready));
        else // Otherwise we can add the reference now
          view->add_nested_resource_ref(did);
      }
    }

    //--------------------------------------------------------------------------
    RtEvent PhiView::defer_add_reference(DistributedCollectable *dc,
                                         RtEvent precondition) const
    //--------------------------------------------------------------------------
    {
      DeferPhiViewRefArgs args(dc, did);
      return context->runtime->issue_runtime_meta_task(args,
          LG_LATENCY_DEFERRED_PRIORITY, precondition);
    }

    //--------------------------------------------------------------------------
    void PhiView::send_view(AddressSpaceID target)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(is_owner());
      assert(collective_mapping == NULL);
#endif
      Serializer rez;
      {
        RezCheck z(rez);
        rez.serialize(did);
        rez.serialize(owner_space);
        rez.serialize(true_guard);
        rez.serialize(false_guard);
        rez.serialize<UniqueID>(owner_context->get_context_uid());
        pack_phi_view(rez);
      }
      runtime->send_phi_view(target, rez);
      update_remote_instances(target);
    }

    //--------------------------------------------------------------------------
    void PhiView::flatten(CopyFillAggregator &aggregator,
                         InstanceView *dst_view, const FieldMask &src_mask,
                         IndexSpaceExpression *expr, EquivalenceSet *tracing_eq,
                         std::set<RtEvent> &applied, CopyAcrossHelper *helper)
    //--------------------------------------------------------------------------
    {
      // TODO: implement this
      assert(false);
    }

    //--------------------------------------------------------------------------
    /*static*/ void PhiView::handle_send_phi_view(Runtime *runtime,
                                     Deserializer &derez, AddressSpaceID source)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez);
      DistributedID did;
      derez.deserialize(did);
      AddressSpaceID owner;
      derez.deserialize(owner);
      PredEvent true_guard, false_guard;
      derez.deserialize(true_guard);
      derez.deserialize(false_guard);
      UniqueID owner_uid;
      derez.deserialize(owner_uid);
      std::set<RtEvent> ready_events;
      RtEvent ctx_ready;
      InnerContext *owner_context = 
        runtime->find_context(owner_uid, false, &ctx_ready);
      if (ctx_ready.exists())
        ready_events.insert(ctx_ready);
      // Make the phi view but don't register it yet
      void *location;
      PhiView *view = NULL;
      if (runtime->find_pending_collectable_location(did, location))
        view = new(location) PhiView(runtime->forest, did, owner,
                                     true_guard, false_guard, owner_context, 
                                     false/*register_now*/);
      else
        view = new PhiView(runtime->forest, did, owner, true_guard, 
                           false_guard, owner_context, false/*register now*/);
      // Unpack all the internal data structures
      view->unpack_phi_view(derez, ready_events);
      if (!ready_events.empty())
      {
        RtEvent wait_on = Runtime::merge_events(ready_events);
        DeferPhiViewRegistrationArgs args(view);
        runtime->issue_runtime_meta_task(args, LG_LATENCY_DEFERRED_PRIORITY,
                                         wait_on);
        return;
      }
      view->register_with_runtime();
    }

    //--------------------------------------------------------------------------
    /*static*/ void PhiView::handle_deferred_view_ref(const void *args)
    //--------------------------------------------------------------------------
    {
      const DeferPhiViewRefArgs *rargs = (const DeferPhiViewRefArgs*)args;
      rargs->dc->add_nested_resource_ref(rargs->did); 
    }

    //--------------------------------------------------------------------------
    /*static*/ void PhiView::handle_deferred_view_registration(const void *args)
    //--------------------------------------------------------------------------
    {
      const DeferPhiViewRegistrationArgs *pargs = 
        (const DeferPhiViewRegistrationArgs*)args;
      pargs->view->register_with_runtime();
    }

    /////////////////////////////////////////////////////////////
    // ReductionView 
    /////////////////////////////////////////////////////////////

    //--------------------------------------------------------------------------
    ReductionView::ReductionView(RegionTreeForest *ctx, DistributedID did,
                                 AddressSpaceID own_sp,
                                 AddressSpaceID log_own,
                                 PhysicalManager *man, UniqueID own_ctx, 
                                 bool register_now, CollectiveMapping *mapping)
      : IndividualView(ctx, encode_reduction_did(did), man, own_sp, log_own, 
                       own_ctx, register_now, mapping)
    //--------------------------------------------------------------------------
    {
#ifdef LEGION_GC
      log_garbage.info("GC Reduction View %lld %d %lld", 
          LEGION_DISTRIBUTED_ID_FILTER(this->did), local_space,
          LEGION_DISTRIBUTED_ID_FILTER(manager->did));
#endif
    }

    //--------------------------------------------------------------------------
    ReductionView::~ReductionView(void)
    //--------------------------------------------------------------------------
    { 
      if (!initial_user_events.empty())
      {
        for (std::set<ApEvent>::const_iterator it = initial_user_events.begin();
              it != initial_user_events.end(); it++)
          filter_local_users(*it);
      }
#if !defined(LEGION_DISABLE_EVENT_PRUNING) && defined(DEBUG_LEGION)
      assert(writing_users.empty());
      assert(reduction_users.empty());
      assert(reading_users.empty());
      assert(outstanding_gc_events.empty());
#endif
    }

    //--------------------------------------------------------------------------
    void ReductionView::add_initial_user(ApEvent term_event, 
                                         const RegionUsage &usage,
                                         const FieldMask &user_mask,
                                         IndexSpaceExpression *user_expr,
                                         const UniqueID op_id,
                                         const unsigned index)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(is_logical_owner());
      assert(IS_READ_ONLY(usage) || IS_REDUCE(usage));
#endif
      // We don't use field versions for doing interference tests on
      // reductions so there is no need to record it
#ifdef ENABLE_VIEW_REPLICATION
      PhysicalUser *user = new PhysicalUser(usage, user_expr, op_id, index, 
                                term_event, false/*copy*/, true/*covers*/);
#else
      PhysicalUser *user = new PhysicalUser(usage, user_expr, op_id, index, 
                                            false/*copy*/, true/*covers*/);
#endif
      user->add_reference();
      add_physical_user(user, IS_READ_ONLY(usage), term_event, user_mask);
      initial_user_events.insert(term_event);
      // Don't need to actual launch a collection task, destructor
      // will handle this case
      outstanding_gc_events.insert(term_event);
    }

    //--------------------------------------------------------------------------
    ApEvent ReductionView::register_user(const RegionUsage &usage,
                                         const FieldMask &user_mask,
                                         IndexSpaceNode *user_expr,
                                         const UniqueID op_id,
                                         const size_t op_ctx_index,
                                         const unsigned index,
                                         ApEvent term_event,
                                         RtEvent collect_event,
                                         PhysicalManager *target,
                                         size_t local_collective_arrivals,
                                         std::set<RtEvent> &applied_events,
                                         const PhysicalTraceInfo &trace_info,
                                         const AddressSpaceID source,
                                         const bool symbolic /*=false*/)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(usage.redop == manager->redop);
      assert(target == manager);
#endif
      // Handle the collective rendezvous if necessary
      if (local_collective_arrivals > 0)
        return register_collective_user(usage, user_mask, user_expr,
              op_id, op_ctx_index, index, term_event, collect_event,
              target, local_collective_arrivals, applied_events,
              trace_info, symbolic);
      // Quick test for empty index space expressions
      if (!symbolic && user_expr->is_empty())
        return manager->get_use_event(term_event);
      if (!is_logical_owner())
      {
        // If we're not the logical owner send a message there 
        // to do the analysis and provide a user event to trigger
        // with the precondition
        ApUserEvent ready_event = Runtime::create_ap_user_event(&trace_info);
        RtUserEvent applied_event = Runtime::create_rt_user_event();
        Serializer rez;
        {
          RezCheck z(rez);
          rez.serialize(did);
          rez.serialize(target->did);
          rez.serialize(usage);
          rez.serialize(user_mask);
          rez.serialize(user_expr->handle);
          rez.serialize(op_id);
          rez.serialize(op_ctx_index);
          rez.serialize(index);
          rez.serialize(term_event);
          rez.serialize(collect_event);
          rez.serialize(local_collective_arrivals);
          rez.serialize(ready_event);
          rez.serialize(applied_event);
          trace_info.pack_trace_info(rez, applied_events);
        }
        // Add a remote valid reference that will be removed by 
        // the receiver once the changes have been applied
        WrapperReferenceMutator mutator(applied_events);
        add_base_valid_ref(REMOTE_DID_REF, &mutator);
        runtime->send_view_register_user(logical_owner, rez);
        applied_events.insert(applied_event);
        return ready_event;
      }
      else
      {
        std::set<ApEvent> wait_on_events;
        ApEvent start_use_event = manager->get_use_event(term_event);
        if (start_use_event.exists())
          wait_on_events.insert(start_use_event);
        // At the moment we treat exclusive reductions the same as
        // atomic reductions, this might change in the future
        const RegionUsage reduce_usage(usage.privilege,
            (usage.prop == LEGION_EXCLUSIVE) ? LEGION_ATOMIC : usage.prop,
            usage.redop);
        {
          AutoLock v_lock(view_lock,1,false/*exclusive*/);
          find_reducing_preconditions(reduce_usage, user_mask,
                                      user_expr, wait_on_events);
        }
        // Add our local user
        const bool issue_collect = add_user(reduce_usage, user_expr,
                                      user_mask, term_event, collect_event,
                                      op_id, index, false/*copy*/,
                                      applied_events, trace_info.recording);
        // Launch the garbage collection task, if it doesn't exist
        // then the user wasn't registered anyway, see add_local_user
        if (issue_collect)
        {
          WrapperReferenceMutator mutator(applied_events);
          defer_collect_user(get_manager(), term_event, collect_event,&mutator);
        }
        if (!wait_on_events.empty())
          return Runtime::merge_events(&trace_info, wait_on_events);
        else
          return ApEvent::NO_AP_EVENT;
      }
    }

    //--------------------------------------------------------------------------
    ApEvent ReductionView::find_copy_preconditions(bool reading,
                                            ReductionOpID redop,
                                            const FieldMask &copy_mask,
                                            IndexSpaceExpression *copy_expr,
                                            UniqueID op_id, unsigned index,
                                            std::set<RtEvent> &applied_events,
                                            const PhysicalTraceInfo &trace_info)
    //--------------------------------------------------------------------------
    {
      if (!is_logical_owner())
      {
        ApUserEvent ready_event = Runtime::create_ap_user_event(&trace_info);
        RtUserEvent applied = Runtime::create_rt_user_event();
        Serializer rez;
        {
          RezCheck z(rez);
          rez.serialize(did);
          rez.serialize<bool>(reading);
          rez.serialize(redop);
          rez.serialize(copy_mask);
          copy_expr->pack_expression(rez, logical_owner);
          rez.serialize(op_id);
          rez.serialize(index);
          rez.serialize(ready_event);
          rez.serialize(applied);
          trace_info.pack_trace_info(rez, applied_events);
        }
        runtime->send_view_find_copy_preconditions_request(logical_owner, rez);
        applied_events.insert(applied);
        return ready_event;
      }
      else
      {
        std::set<ApEvent> preconditions;
        ApEvent start_use_event = manager->get_use_event();
        if (start_use_event.exists())
          preconditions.insert(start_use_event);
        if (reading)
        {
          AutoLock v_lock(view_lock,1,false/*exclusive*/);
          find_reading_preconditions(copy_mask, copy_expr, preconditions);
        }
        else if (redop > 0)
        {
#ifdef DEBUG_LEGION
          assert(redop == manager->redop);
#endif
          // With bulk reduction copies we're always doing atomic reductions
          const RegionUsage usage(LEGION_REDUCE, LEGION_ATOMIC, redop);
          AutoLock v_lock(view_lock,1,false/*exclusive*/);
          find_reducing_preconditions(usage,copy_mask,copy_expr,preconditions);
        }
        else
        {
          AutoLock v_lock(view_lock);
          find_writing_preconditions(copy_mask, copy_expr, preconditions);
        }
        // Return any preconditions we found to the aggregator
        if (preconditions.empty())
          return ApEvent::NO_AP_EVENT;
        return Runtime::merge_events(&trace_info, preconditions);
      }
    }

    //--------------------------------------------------------------------------
    void ReductionView::add_copy_user(bool reading, ReductionOpID redop,
                                      ApEvent term_event, RtEvent collect_event,
                                      const FieldMask &copy_mask,
                                      IndexSpaceExpression *copy_expr,
                                      UniqueID op_id, unsigned index,
                                      std::set<RtEvent> &applied_events,
                                      const bool trace_recording,
                                      const AddressSpaceID source)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      // At most one of these should be true 
      assert(!(reading && (redop > 0)));
#endif
      if (!is_logical_owner())
      {
        RtUserEvent applied_event = Runtime::create_rt_user_event();
        Serializer rez;
        {
          RezCheck z(rez);
          rez.serialize(did);
          rez.serialize<bool>(reading);
          rez.serialize(redop);
          rez.serialize(term_event);
          rez.serialize(collect_event);
          rez.serialize(copy_mask);
          copy_expr->pack_expression(rez, logical_owner);
          rez.serialize(op_id);
          rez.serialize(index);
          rez.serialize(applied_event);
          rez.serialize<bool>(trace_recording);
        }
        // Add a remote valid reference that will be removed by 
        // the receiver once the changes have been applied
        WrapperReferenceMutator mutator(applied_events);
        add_base_valid_ref(REMOTE_DID_REF, &mutator);
        runtime->send_view_add_copy_user(logical_owner, rez);
        applied_events.insert(applied_event);
      }
      else
      {
        const RegionUsage usage(reading ? LEGION_READ_ONLY : (redop > 0) ?
            LEGION_REDUCE : LEGION_READ_WRITE, LEGION_EXCLUSIVE, redop);
        const bool issue_collect = add_user(usage, copy_expr, copy_mask,
            term_event, collect_event, op_id, index, true/*copy*/,
            applied_events, trace_recording);
        // Launch the garbage collection task, if it doesn't exist
        // then the user wasn't registered anyway, see add_local_user
        if (issue_collect)
        {
          WrapperReferenceMutator mutator(applied_events);
          defer_collect_user(get_manager(), term_event, collect_event,&mutator);
        }
      }
    }

    //--------------------------------------------------------------------------
    void ReductionView::find_last_users(PhysicalManager *instance,
                                        std::set<ApEvent> &events,
                                        const RegionUsage &usage,
                                        const FieldMask &mask,
                                        IndexSpaceExpression *expr,
                                        std::vector<RtEvent> &ready_events)const
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(instance == manager);
#endif
      // Check to see if we're on the right node to perform this analysis
      if (logical_owner != local_space)
      {
        const RtUserEvent ready = Runtime::create_rt_user_event();
        Serializer rez;
        {
          RezCheck z(rez);
          rez.serialize(did);
          rez.serialize(instance->did);
          rez.serialize(&events);
          rez.serialize(usage);
          rez.serialize(mask);
          expr->pack_expression(rez, logical_owner);
          rez.serialize(ready);
        }
        runtime->send_view_find_last_users_request(logical_owner, rez);
        ready_events.push_back(ready);
      }
      else
      {
        if (IS_READ_ONLY(usage))
        {
          AutoLock v_lock(view_lock,1,false/*exclusive*/);
          find_reading_preconditions(mask, expr, events);
        }
        else if (usage.redop > 0)
        {
#ifdef DEBUG_LEGION
          assert(usage.redop == manager->redop);
#endif
          // With bulk reduction copies we're always doing atomic reductions
          AutoLock v_lock(view_lock,1,false/*exclusive*/);
          find_reducing_preconditions(usage, mask, expr, events);
        }
        else
        {
          AutoLock v_lock(view_lock,1,false/*exclusive*/);
          find_initializing_last_users(mask, expr, events);
        }
      }
    }

    //--------------------------------------------------------------------------
    void ReductionView::find_reducing_preconditions(const RegionUsage &usage,
                                               const FieldMask &user_mask,
                                               IndexSpaceExpression *user_expr,
                                               std::set<ApEvent> &wait_on) const
    //--------------------------------------------------------------------------
    {
      // lock must be held by caller
      find_dependences(writing_users, user_expr, user_mask, wait_on);
      find_dependences(reading_users, user_expr, user_mask, wait_on);
      // check for coherence dependences on previous reduction users
      for (EventFieldUsers::const_iterator uit = reduction_users.begin();
            uit != reduction_users.end(); uit++)
      {
        const FieldMask event_mask = uit->second.get_valid_mask() & user_mask;
        if (!event_mask)
          continue;
        for (EventUsers::const_iterator it = uit->second.begin();
              it != uit->second.end(); it++)
        {
#ifdef DEBUG_LEGION
          assert(it->first->usage.redop == usage.redop);
#endif
          const FieldMask overlap = event_mask & it->second;
          if (!overlap)
            continue;
          // If they are both simultaneous then we can skip
          if (IS_SIMULT(usage) && IS_SIMULT(it->first->usage))
            continue;
          // Atomic and exclusive are the same for the purposes of reductions
          // at the moment since we'll end up using the reservations to 
          // protect the use of the instance anyway
          if ((IS_EXCLUSIVE(usage) || IS_ATOMIC(usage)) && 
              (IS_EXCLUSIVE(it->first->usage) || IS_ATOMIC(it->first->usage)))
            continue;
          // Otherwise we need to check for dependences
          IndexSpaceExpression *expr_overlap = 
            context->intersect_index_spaces(user_expr, it->first->expr);
          if (expr_overlap->is_empty())
            continue;
          wait_on.insert(uit->first);
        }
      }
    }

    //--------------------------------------------------------------------------
    void ReductionView::find_dependences(const EventFieldUsers &users,
                                         IndexSpaceExpression *user_expr,
                                         const FieldMask &user_mask,
                                         std::set<ApEvent> &wait_on) const
    //--------------------------------------------------------------------------
    {
      for (EventFieldUsers::const_iterator uit =
            users.begin(); uit != users.end(); uit++)
      {
        const FieldMask event_mask = uit->second.get_valid_mask() & user_mask;
        if (!event_mask)
          continue;
        for (EventUsers::const_iterator it = uit->second.begin();
              it != uit->second.end(); it++)
        {
          const FieldMask overlap = event_mask & it->second;
          if (!overlap)
            continue;
          IndexSpaceExpression *expr_overlap = 
            context->intersect_index_spaces(user_expr, it->first->expr);
          if (expr_overlap->is_empty())
            continue;
          wait_on.insert(uit->first);
          break;
        }
      }
    }

    //--------------------------------------------------------------------------
    void ReductionView::find_writing_preconditions(
                                                const FieldMask &user_mask,
                                                IndexSpaceExpression *user_expr,
                                                std::set<ApEvent> &wait_on)
    //--------------------------------------------------------------------------
    {
      // lock must be held by caller
      find_dependences_and_filter(writing_users, user_expr, user_mask, wait_on); 
      find_dependences_and_filter(reduction_users, user_expr,user_mask,wait_on);
      find_dependences_and_filter(reading_users, user_expr, user_mask, wait_on);
    }

    //--------------------------------------------------------------------------
    void ReductionView::find_dependences_and_filter(EventFieldUsers &users,
                                                IndexSpaceExpression *user_expr,
                                                const FieldMask &user_mask,
                                                std::set<ApEvent> &wait_on)
    //--------------------------------------------------------------------------
    {
      for (EventFieldUsers::iterator uit = users.begin();
            uit != users.end(); /*nothing*/)
      {
        FieldMask event_mask = uit->second.get_valid_mask() & user_mask;
        if (!event_mask)
        {
          uit++;
          continue;
        }
        std::vector<PhysicalUser*> to_delete;
        for (EventUsers::iterator it = uit->second.begin();
              it != uit->second.end(); it++)
        {
          const FieldMask overlap = event_mask & it->second;
          if (!overlap)
            continue;
          IndexSpaceExpression *expr_overlap = 
            context->intersect_index_spaces(user_expr, it->first->expr);
          if (expr_overlap->is_empty())
            continue;
          // Have a precondition so we need to record it
          wait_on.insert(uit->first);
          // See if we can prune out this user because it is dominated
          if (expr_overlap->get_volume() == it->first->expr->get_volume())
          {
            it.filter(overlap);
            if (!it->second)
              to_delete.push_back(it->first);
          }
          // If we've captured a dependence on this event for every
          // field then we can exit out early
          event_mask -= overlap;
          if (!event_mask)
            break;
        }
        if (!to_delete.empty())
        {
          for (std::vector<PhysicalUser*>::const_iterator it = 
                to_delete.begin(); it != to_delete.end(); it++)
          {
            uit->second.erase(*it);
            if ((*it)->remove_reference())
              delete (*it);
          }
          if (uit->second.empty())
          {
            EventFieldUsers::iterator to_erase = uit++;
            users.erase(to_erase);
          }
          else
          {
            uit->second.tighten_valid_mask();
            uit++;
          }
        }
        else
          uit++;
      }
    }

    //--------------------------------------------------------------------------
    void ReductionView::find_reading_preconditions(const FieldMask &user_mask,
                                         IndexSpaceExpression *user_expr,
                                         std::set<ApEvent> &preconditions) const
    //--------------------------------------------------------------------------
    {
      // lock must be held by caller
      find_dependences(writing_users, user_expr, user_mask, preconditions);
      find_dependences(reduction_users, user_expr, user_mask, preconditions);
    }

    //--------------------------------------------------------------------------
    void ReductionView::find_initializing_last_users(
                                         const FieldMask &user_mask,
                                         IndexSpaceExpression *user_expr,
                                         std::set<ApEvent> &preconditions) const
    //--------------------------------------------------------------------------
    {
      // lock must be held by caller
      // we know that reduces dominate earlier fills so we don't need to check
      // those, but we do need to check both reducers and readers since it is
      // possible there were no readers of reduction instance
      for (EventFieldUsers::const_iterator uit = reduction_users.begin();
            uit != reduction_users.end(); uit++)
      {
        FieldMask event_mask = uit->second.get_valid_mask() & user_mask;
        if (!event_mask)
          continue;
        for (EventUsers::const_iterator it = uit->second.begin();
              it != uit->second.end(); it++)
        {
          const FieldMask overlap = event_mask & it->second;
          if (!overlap)
            continue;
          IndexSpaceExpression *expr_overlap = 
            context->intersect_index_spaces(user_expr, it->first->expr);
          if (expr_overlap->is_empty())
            continue;
          // Have a precondition so we need to record it
          preconditions.insert(uit->first);
          // If we've captured a dependence on this event for every
          // field then we can exit out early
          event_mask -= overlap;
          if (!event_mask)
            break;
        }
      }
      for (EventFieldUsers::const_iterator uit = reading_users.begin();
            uit != reading_users.end(); uit++)
      {
        FieldMask event_mask = uit->second.get_valid_mask() & user_mask;
        if (!event_mask)
          continue;
        for (EventUsers::const_iterator it = uit->second.begin();
              it != uit->second.end(); it++)
        {
          const FieldMask overlap = event_mask & it->second;
          if (!overlap)
            continue;
          IndexSpaceExpression *expr_overlap = 
            context->intersect_index_spaces(user_expr, it->first->expr);
          if (expr_overlap->is_empty())
            continue;
          // Have a precondition so we need to record it
          preconditions.insert(uit->first);
          // If we've captured a dependence on this event for every
          // field then we can exit out early
          event_mask -= overlap;
          if (!event_mask)
            break;
        }
      }
    }

    //--------------------------------------------------------------------------
    bool ReductionView::add_user(const RegionUsage &usage,
                                 IndexSpaceExpression *user_expr,
                                 const FieldMask &user_mask, 
                                 ApEvent term_event, RtEvent collect_event,
                                 UniqueID op_id, unsigned index, bool copy_user,
                                 std::set<RtEvent> &applied_events,
                                 const bool trace_recording)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(is_logical_owner());
#endif
#ifdef ENABLE_VIEW_REPLICATION
      PhysicalUser *new_user = new PhysicalUser(usage, user_expr, op_id, index, 
                                     collect_event, copy_user, true/*covers*/);
#else
      PhysicalUser *new_user = new PhysicalUser(usage, user_expr, op_id, index, 
                                                copy_user, true/*covers*/);
#endif
      new_user->add_reference();
      // No matter what, we retake the lock in exclusive mode so we
      // can handle any clean-up and add our user
      AutoLock v_lock(view_lock);
      add_physical_user(new_user, IS_READ_ONLY(usage), term_event, user_mask);

      if (outstanding_gc_events.find(term_event) == outstanding_gc_events.end())
      {
        outstanding_gc_events.insert(term_event);
        return true;
      }
      else
        return false;
    }

    //--------------------------------------------------------------------------
    void ReductionView::add_physical_user(PhysicalUser *user, bool reading,
                                          ApEvent term_event, 
                                          const FieldMask &user_mask)
    //--------------------------------------------------------------------------
    {
      // Better already be holding the lock
      EventUsers &event_users = reading ? reading_users[term_event] : 
                 IS_REDUCE(user->usage) ? reduction_users[term_event] : 
                                          writing_users[term_event];
#ifdef DEBUG_LEGION
      assert(event_users.find(user) == event_users.end());
#endif
      event_users.insert(user, user_mask);
    }

    //--------------------------------------------------------------------------
    void ReductionView::filter_local_users(ApEvent term_event)
    //--------------------------------------------------------------------------
    {
      DETAILED_PROFILER(context->runtime, 
                        REDUCTION_VIEW_FILTER_LOCAL_USERS_CALL);
      // Better be holding the lock before calling this
      std::set<ApEvent>::iterator event_finder = 
        outstanding_gc_events.find(term_event);
      if (event_finder != outstanding_gc_events.end())
      {
        EventFieldUsers::iterator finder = writing_users.find(term_event);
        if (finder != writing_users.end())
        {
          for (EventUsers::const_iterator it = finder->second.begin();
                it != finder->second.end(); it++)
            if (it->first->remove_reference())
              delete it->first;
          writing_users.erase(finder);
        }
        finder = reduction_users.find(term_event);
        if (finder != reduction_users.end())
        {
          for (EventUsers::const_iterator it = finder->second.begin();
                it != finder->second.end(); it++)
            if (it->first->remove_reference())
              delete it->first;
          reduction_users.erase(finder);
        }
        finder = reading_users.find(term_event);
        if (finder != reading_users.end())
        {
          for (EventUsers::const_iterator it = finder->second.begin();
                it != finder->second.end(); it++)
            if (it->first->remove_reference())
              delete it->first;
          reading_users.erase(finder);
        }
        outstanding_gc_events.erase(event_finder);
      }
    }

    //--------------------------------------------------------------------------
    void ReductionView::add_collectable_reference(ReferenceMutator *mutator)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(mutator != NULL);
#endif
      // Only the logical owner adds the full GC reference as this is where
      // the actual garbage collection algorithm will take place and we know
      // that we have all the valid gc event users
      if (is_logical_owner())
        add_base_gc_ref(PENDING_GC_REF, mutator);
      else
        add_base_resource_ref(PENDING_GC_REF);
    }

    //--------------------------------------------------------------------------
    bool ReductionView::remove_collectable_reference(ReferenceMutator *mutator)
    //--------------------------------------------------------------------------
    {
      if (is_logical_owner())
        return remove_base_gc_ref(PENDING_GC_REF, mutator);
      else
        return remove_base_resource_ref(PENDING_GC_REF);
    }

    //--------------------------------------------------------------------------
    void ReductionView::collect_users(const std::set<ApEvent> &term_events)
    //--------------------------------------------------------------------------
    {
      // Do not do this if we are in LegionSpy so we can see 
      // all of the dependences
#ifndef LEGION_DISABLE_EVENT_PRUNING
      AutoLock v_lock(view_lock);
      for (std::set<ApEvent>::const_iterator it = term_events.begin();
            it != term_events.end(); it++)
      {
        filter_local_users(*it); 
      }
#endif
    }

    //--------------------------------------------------------------------------
    void ReductionView::send_view(AddressSpaceID target)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(is_owner());
#endif
      // Check to see if this is a replicated view, if the target
      // is in the replicated set, then there's nothing we need to do
      // We can just ignore this and the registration will be done later
      if ((collective_mapping != NULL) && collective_mapping->contains(target))
        return;
      // Don't take the lock, it's alright to have duplicate sends
      Serializer rez;
      {
        RezCheck z(rez);
        rez.serialize(did);
        rez.serialize(manager->did);
        rez.serialize(owner_space);
        rez.serialize(logical_owner);
        rez.serialize(owner_context);
      }
      runtime->send_reduction_view(target, rez);
      update_remote_instances(target);
    }

    //--------------------------------------------------------------------------
    ReductionOpID ReductionView::get_redop(void) const
    //--------------------------------------------------------------------------
    {
      return manager->redop;
    }

    //--------------------------------------------------------------------------
    /*static*/ void ReductionView::handle_send_reduction_view(Runtime *runtime,
                                     Deserializer &derez, AddressSpaceID source)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez); 
      DistributedID did;
      derez.deserialize(did);
      DistributedID manager_did;
      derez.deserialize(manager_did);
      AddressSpaceID owner_space;
      derez.deserialize(owner_space);
      AddressSpaceID logical_owner;
      derez.deserialize(logical_owner);
      UniqueID context_uid;
      derez.deserialize(context_uid);

      RtEvent man_ready;
      PhysicalManager *manager =
        runtime->find_or_request_instance_manager(manager_did, man_ready);
      if (man_ready.exists() && !man_ready.has_triggered())
      {
        // Defer this until the manager is ready
        DeferReductionViewArgs args(did, manager, owner_space,
                                    logical_owner, context_uid);
        runtime->issue_runtime_meta_task(args,
            LG_LATENCY_RESPONSE_PRIORITY, man_ready);
      }
      else
        create_remote_view(runtime, did, manager, owner_space, 
                           logical_owner, context_uid);
    }

    //--------------------------------------------------------------------------
    /*static*/ void ReductionView::handle_defer_reduction_view(
                                             const void *args, Runtime *runtime)
    //--------------------------------------------------------------------------
    {
      const DeferReductionViewArgs *dargs = 
        (const DeferReductionViewArgs*)args; 
      create_remote_view(runtime, dargs->did, dargs->manager, 
          dargs->owner_space, dargs->logical_owner, dargs->context_uid);
    }

    //--------------------------------------------------------------------------
    /*static*/ void ReductionView::create_remote_view(Runtime *runtime,
                            DistributedID did, PhysicalManager *manager,
                            AddressSpaceID owner_space, 
                            AddressSpaceID logical_owner, UniqueID context_uid)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(manager->is_reduction_manager());
#endif
      void *location;
      ReductionView *view = NULL;
      if (runtime->find_pending_collectable_location(did, location))
        view = new(location) ReductionView(runtime->forest, did, owner_space, 
                                           logical_owner, manager,
                                           context_uid, false/*register now*/);
      else
        view = new ReductionView(runtime->forest, did, owner_space,
                                 logical_owner, manager, 
                                 context_uid, false/*register now*/);
      // Only register after construction
      view->register_with_runtime();
    }

    /////////////////////////////////////////////////////////////
    // CollectiveView
    /////////////////////////////////////////////////////////////

    //--------------------------------------------------------------------------
    CollectiveView::CollectiveView(RegionTreeForest *ctx, DistributedID id,
                                   AddressSpaceID owner_proc, 
                                   UniqueID owner_context, 
                                   const std::vector<IndividualView*> &views,
                                   bool register_now,CollectiveMapping *mapping)
      : InstanceView(ctx, id, owner_proc, owner_context, register_now, mapping),
        local_views(views) 
    //--------------------------------------------------------------------------
    {
      for (std::vector<IndividualView*>::const_iterator it =
            local_views.begin(); it != local_views.end(); it++)
      {
#ifdef DEBUG_LEGION
        // For collective instances we always want the logical analysis 
        // node for the view to be on the same node as the owner for actual
        // physical instance to aid in our ability to do the analysis
        // See the get_analysis_space function for why we check this
        assert((*it)->logical_owner == (*it)->get_manager()->owner_space);
#endif
        (*it)->add_nested_resource_ref(did);
      }
    }

    //--------------------------------------------------------------------------
    CollectiveView::~CollectiveView(void)
    //--------------------------------------------------------------------------
    {
      for (std::vector<IndividualView*>::const_iterator it =
            local_views.begin(); it != local_views.end(); it++)
        if ((*it)->remove_nested_resource_ref(did))
          delete (*it);
      for (std::set<PhysicalManager*>::const_iterator it =
            remote_instances.begin(); it != remote_instances.end(); it++)
        if ((*it)->remove_nested_resource_ref(did))
          delete (*it);
    }

    //--------------------------------------------------------------------------
    AddressSpaceID CollectiveView::get_analysis_space(
                                                PhysicalManager *instance) const
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(contains(instance));
#endif
      return instance->owner_space;
    }

    //--------------------------------------------------------------------------
    void CollectiveView::notify_active(ReferenceMutator *mutator)
    //--------------------------------------------------------------------------
    {
      // Propagate gc references to all the children
      if ((collective_mapping != NULL) && 
          collective_mapping->contains(local_space))
      {
        std::vector<AddressSpaceID> children;
        collective_mapping->get_children(owner_space, local_space, children);
        for (unsigned idx = 0; idx < children.size(); idx++)
          send_remote_gc_increment(children[idx], mutator);
      }
      // Add valid references to our local views
      for (unsigned idx = 0; idx < local_views.size(); idx++)
        local_views[idx]->add_nested_valid_ref(did, mutator);
    }

    //--------------------------------------------------------------------------
    void CollectiveView::notify_inactive(ReferenceMutator *mutator)
    //--------------------------------------------------------------------------
    {
      // Remove gc references from all the children
      if ((collective_mapping != NULL) &&
          collective_mapping->contains(local_space))
      {
        std::vector<AddressSpaceID> children;
        collective_mapping->get_children(owner_space, local_space, children);
        for (unsigned idx = 0; idx < children.size(); idx++)
          send_remote_gc_decrement(children[idx], mutator);
      }
      // Remove valid references from our local views
      for (unsigned idx = 0; idx < local_views.size(); idx++)
        local_views[idx]->remove_nested_valid_ref(did, mutator);
    }

    //--------------------------------------------------------------------------
    void CollectiveView::notify_valid(ReferenceMutator *mutator)
    //--------------------------------------------------------------------------
    {
      if (is_owner())
      {
        // Send out gc references to all the children
        if (collective_mapping != NULL)
        {
          std::vector<AddressSpaceID> children;
          collective_mapping->get_children(owner_space, local_space, children);
          for (unsigned idx = 0; idx < children.size(); idx++)
            send_remote_gc_increment(children[idx], mutator);
        }
      }
      else
      {
        // Propagate valid references down towards the owner
        if ((collective_mapping != NULL) &&
            collective_mapping->contains(local_space))
          send_remote_valid_increment(
            collective_mapping->get_parent(owner_space, local_space), mutator);
        else
          send_remote_valid_increment(owner_space, mutator);
      }
    }

    //--------------------------------------------------------------------------
    void CollectiveView::notify_invalid(ReferenceMutator *mutator)
    //--------------------------------------------------------------------------
    {
      if (is_owner())
      {
        // Remove gc references on all the children
        if (collective_mapping != NULL)
        {
          std::vector<AddressSpaceID> children;
          collective_mapping->get_children(owner_space, local_space, children);
          for (unsigned idx = 0; idx < children.size(); idx++)
            send_remote_gc_decrement(children[idx], mutator);
        }
      }
      else
      {
        // Remove valid references down towards the owner
        if ((collective_mapping != NULL) &&
            collective_mapping->contains(local_space))
          send_remote_valid_decrement(
            collective_mapping->get_parent(owner_space, local_space), mutator);
        else
          send_remote_valid_decrement(owner_space, mutator);
      }
    }

    //--------------------------------------------------------------------------
    ApEvent CollectiveView::register_user(const RegionUsage &usage,
                                          const FieldMask &user_mask,
                                          IndexSpaceNode *user_expr,
                                          const UniqueID op_id,
                                          const size_t op_ctx_index,
                                          const unsigned index,
                                          ApEvent term_event,
                                          RtEvent collect_event,
                                          PhysicalManager *target,
                                          size_t local_collective_arrivals,
                                          std::set<RtEvent> &applied_events,
                                          const PhysicalTraceInfo &trace_info,
                                          const AddressSpaceID source,
                                          const bool symbolic /*=false*/)
    //--------------------------------------------------------------------------
    {
      if (local_collective_arrivals > 0)
      {
        // Check to see if we're on the right node for this
        if (!target->is_owner())
        {
          ApUserEvent ready_event = Runtime::create_ap_user_event(&trace_info);
          RtUserEvent applied_event = Runtime::create_rt_user_event();
          Serializer rez;
          {
            RezCheck z(rez);
            rez.serialize(did);
            rez.serialize(target->did);
            rez.serialize(usage);
            rez.serialize(user_mask);
            rez.serialize(user_expr->handle);
            rez.serialize(op_id);
            rez.serialize(op_ctx_index);
            rez.serialize(index);
            rez.serialize(term_event);
            rez.serialize(collect_event);
            rez.serialize(local_collective_arrivals);
            rez.serialize(ready_event);
            rez.serialize(applied_event);
            trace_info.pack_trace_info(rez, applied_events);
          }
          runtime->send_view_register_user(target->owner_space, rez);
          applied_events.insert(applied_event);
          return ready_event;
        }
        else
          return register_collective_user(usage, user_mask, user_expr,
              op_id, op_ctx_index, index, term_event, collect_event,
              target, local_collective_arrivals, applied_events,
              trace_info, symbolic);
      }
#ifdef DEBUG_LEGION
      assert(target->is_owner());
#endif
      // Iterate through our local views and find the view for the target
      for (unsigned idx = 0; idx < local_views.size(); idx++)
        if (local_views[idx]->get_manager() == target)
          return local_views[idx]->register_user(usage, user_mask, 
              user_expr, op_id, op_ctx_index, index, term_event,
              collect_event, target, local_collective_arrivals,
              applied_events, trace_info, source, symbolic);
      // Should never get here
      assert(false);
      return ApEvent::NO_AP_EVENT;
    }

    //--------------------------------------------------------------------------
    void CollectiveView::find_last_users(PhysicalManager *manager,
                                         std::set<ApEvent> &events,
                                         const RegionUsage &usage,
                                         const FieldMask &mask,
                                         IndexSpaceExpression *user_expr,
                                         std::vector<RtEvent> &applied) const
    //--------------------------------------------------------------------------
    {
      const AddressSpaceID analysis_space = get_analysis_space(manager);
      if (analysis_space != local_space)
      {
        const RtUserEvent ready = Runtime::create_rt_user_event();
        Serializer rez;
        {
          RezCheck z(rez);
          rez.serialize(did);
          rez.serialize(manager->did);
          rez.serialize(&events);
          rez.serialize(usage);
          rez.serialize(mask);
          user_expr->pack_expression(rez, analysis_space);
          rez.serialize(ready);
        }
        runtime->send_view_find_last_users_request(analysis_space, rez);
        applied.push_back(ready);
      }
      else
      {
        const unsigned local_index = find_local_index(manager);
        local_views[local_index]->find_last_users(manager, events, usage,
                                                  mask, user_expr, applied);
      }
    }

    //--------------------------------------------------------------------------
    bool CollectiveView::contains(PhysicalManager *manager) const
    //--------------------------------------------------------------------------
    {
      const AddressSpaceID manager_space = get_analysis_space(manager);
      if (manager_space != local_space)
      {
        if ((collective_mapping == NULL) || 
            !collective_mapping->contains(manager_space))
          return false;
        // Check all the current 
        {
          AutoLock v_lock(view_lock,1,false/*exclusive*/);
          std::set<PhysicalManager*>::const_iterator finder = 
            remote_instances.find(manager);
          if (finder != remote_instances.end())
            return true;
          // If we already have all the managers from that node then
          // we don't need to check again
          if (remote_instance_responses.contains(manager_space))
            return false;
        }
        // Send the request and wait for the result
        const RtUserEvent ready_event = Runtime::create_rt_user_event();
        Serializer rez;
        {
          RezCheck z(rez);
          rez.serialize(did);
          rez.serialize(ready_event);
        }
        runtime->send_collective_remote_instances_request(manager_space, rez);
        if (!ready_event.has_triggered())
          ready_event.wait();
        AutoLock v_lock(view_lock,1,false/*exclusive*/);
        return (remote_instances.find(manager) != remote_instances.end());
      }
      else
      {
        for (unsigned idx = 0; idx < local_views.size(); idx++)
          if (local_views[idx]->get_manager() == manager)
            return true;
        return false;
      }
    }

    //--------------------------------------------------------------------------
    bool CollectiveView::meets_regions(
             const std::vector<LogicalRegion> &regions, bool tight_bounds) const
    //--------------------------------------------------------------------------
    {
      if (!local_views.empty())
        return local_views.front()->get_manager()->meets_regions(regions,
                                                                 tight_bounds);
#ifdef DEBUG_LEGION
      assert((collective_mapping == NULL) ||
              !collective_mapping->contains(local_space));
#endif
      PhysicalManager *manager = NULL;
      {
        AutoLock v_lock(view_lock,1,false/*exclusive*/);
        if (!remote_instances.empty())
          manager = *remote_instances.begin();
      }
      if (manager == NULL)
      {
        const AddressSpaceID target_space = (collective_mapping == NULL) ?
          owner_space : collective_mapping->find_nearest(local_space);
        const RtUserEvent ready_event = Runtime::create_rt_user_event();
        Serializer rez;
        {
          RezCheck z(rez);
          rez.serialize(did);
          rez.serialize(ready_event);
        }
        runtime->send_collective_remote_instances_request(target_space, rez);
        if (!ready_event.has_triggered())
          ready_event.wait();
        AutoLock v_lock(view_lock,1,false/*exclusive*/);
#ifdef DEBUG_LEGION
        assert(!remote_instances.empty());
#endif
        manager = *remote_instances.begin();
      }
      return manager->meets_regions(regions, tight_bounds);
    }

    //--------------------------------------------------------------------------
    void CollectiveView::find_instances_in_memory(Memory memory,
                                       std::vector<PhysicalManager*> &instances)
    //--------------------------------------------------------------------------
    {
      const AddressSpaceID memory_space = memory.address_space();
      if (memory_space != local_space)
      {
        // No point checking if we know that it won't have it
        if ((collective_mapping == NULL) ||
            !collective_mapping->contains(memory_space))
          return;
        {
          AutoLock v_lock(view_lock,1,false/*exclusive*/);
          // See if we need the check
          if (remote_instance_responses.contains(memory_space))
          {
            for (std::set<PhysicalManager*>::const_iterator it =
                  remote_instances.begin(); it != remote_instances.end(); it++)
              if ((*it)->memory_manager->memory == memory)
                instances.push_back(*it);
            return;
          }
        }
        const RtUserEvent ready_event = Runtime::create_rt_user_event();
        Serializer rez;
        {
          RezCheck z(rez);
          rez.serialize(did);
          rez.serialize(ready_event);
        }
        runtime->send_collective_remote_instances_request(memory_space, rez);
        if (!ready_event.has_triggered())
          ready_event.wait();
        AutoLock v_lock(view_lock,1,false/*exclusive*/);
        for (std::set<PhysicalManager*>::const_iterator it =
              remote_instances.begin(); it != remote_instances.end(); it++)
          if ((*it)->memory_manager->memory == memory)
            instances.push_back(*it);
      }
      else
      {
        for (unsigned idx = 0; idx < local_views.size(); idx++)
        {
          PhysicalManager *manager = local_views[idx]->get_manager();
          if (manager->memory_manager->memory == memory)
            instances.push_back(manager);
        }
      }
    }

    //--------------------------------------------------------------------------
    /*static*/ void CollectiveView::handle_remote_instances_request(
                   Runtime *runtime, Deserializer &derez, AddressSpaceID source)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez);
      DistributedID did;
      derez.deserialize(did);
      RtEvent ready;
      CollectiveView *view = static_cast<CollectiveView*>(
          runtime->find_or_request_logical_view(did, ready));
      RtUserEvent done;
      derez.deserialize(done);

      if (ready.exists() && !ready.has_triggered())
        ready.wait();
#ifdef DEBUG_LEGION
      assert(!view->local_views.empty());
#endif
      Serializer rez;
      {
        RezCheck z2(rez);
        rez.serialize(did);
        rez.serialize<size_t>(view->local_views.size());
        for (unsigned idx = 0; idx < view->local_views.size(); idx++)
          rez.serialize(view->local_views[idx]->get_manager()->did);
        rez.serialize(done);
      }
      runtime->send_collective_remote_instances_response(source, rez);
    }

    //--------------------------------------------------------------------------
    void CollectiveView::process_remote_instances_response(AddressSpaceID src,
                                  const std::vector<PhysicalManager*> &managers)
    //--------------------------------------------------------------------------
    {
      AutoLock v_lock(view_lock);
      for (std::vector<PhysicalManager*>::const_iterator it =
            managers.begin(); it != managers.end(); it++)
        // Make sure to deduplicate across multiple requests returning
        // the same manaagers in parallel
        if (remote_instances.insert(*it).second)
          (*it)->add_nested_resource_ref(did);
      remote_instance_responses.add(src);
    }

    //--------------------------------------------------------------------------
    /*static*/ void CollectiveView::handle_remote_instances_response(
                   Runtime *runtime, Deserializer &derez, AddressSpaceID source)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez);
      DistributedID did;
      derez.deserialize(did);
      RtEvent ready;
      CollectiveView *view = static_cast<CollectiveView*>(
          runtime->find_or_request_logical_view(did, ready));
      std::vector<RtEvent> ready_events;
      if (ready.exists())
        ready_events.push_back(ready);
      size_t num_instances;
      derez.deserialize(num_instances);
      std::vector<PhysicalManager*> instances(num_instances);
      for (unsigned idx = 0; idx < num_instances; idx++)
      {
        derez.deserialize(did);
        instances[idx] = runtime->find_or_request_instance_manager(did, ready);
        if (ready.exists())
          ready_events.push_back(ready);
      }
      RtUserEvent done;
      derez.deserialize(done);

      if (ready_events.empty())
      {
        const RtEvent wait_on = Runtime::merge_events(ready_events);
        if (wait_on.exists() && !wait_on.has_triggered())
          wait_on.wait();
      }
      view->process_remote_instances_response(source, instances);
      Runtime::trigger_event(done);
    }

    //--------------------------------------------------------------------------
    unsigned CollectiveView::find_local_index(PhysicalManager *target) const
    //--------------------------------------------------------------------------
    {
      for (unsigned idx = 0; idx < local_views.size(); idx++)
        if (local_views[idx]->get_manager() == target)
          return idx;
      // We should always find it
      assert(false);
      return 0;
    }

    //--------------------------------------------------------------------------
    void CollectiveView::register_collective_analysis(PhysicalManager *target,
                 CollectiveAnalysis *analysis, size_t local_collective_arrivals)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(local_collective_arrivals > 0);
#endif
      // First check to see if we are on the right node for this target
      const AddressSpaceID analysis_space = get_analysis_space(target);
      if (analysis_space != local_space)
      {
        Serializer rez;
        {
          RezCheck z(rez);
          rez.serialize(did);
          rez.serialize(target->did);
          analysis->pack_collective_analysis(rez);
          rez.serialize(local_collective_arrivals);
        }
        runtime->send_collective_remote_registration(analysis_space, rez);
        return;
      }
      const unsigned local_index = find_local_index(target);
      const RendezvousKey key(analysis->get_context_index(), 
                              analysis->get_requirement_index());
      AutoLock v_lock(view_lock);
      std::map<RendezvousKey,UserRendezvous>::iterator finder =
        rendezvous_users.find(key);
      if (finder == rendezvous_users.end())
      {
        finder = rendezvous_users.insert(
            std::make_pair(key,UserRendezvous())).first; 
        UserRendezvous &rendezvous = finder->second;
        // Count how many expected arrivals we have
        rendezvous.local_initialized = false;
        rendezvous.remaining_remote_arrivals =
          collective_mapping->count_children(owner_space, local_space);
        rendezvous.local_registered = Runtime::create_rt_user_event();
        rendezvous.global_registered = Runtime::create_rt_user_event();
      }
      // Perform the registration
      if (finder->second.analyses.empty())
      {
        finder->second.analyses.resize(local_views.size(), NULL);
        finder->second.remaining_analyses = local_collective_arrivals;
      }
#ifdef DEBUG_LEGION
      assert(local_index < finder->second.analyses.size());
      assert(finder->second.remaining_analyses > 0);
#endif
      // Only need to save it if we're the first ones for this local view
      if (finder->second.analyses[local_index] == NULL)
      {
        finder->second.analyses[local_index] = analysis;
        analysis->add_analysis_reference();
      }
      if ((--finder->second.remaining_analyses == 0) &&
          finder->second.analyses_ready.exists())
        Runtime::trigger_event(finder->second.analyses_ready);
    }

    //--------------------------------------------------------------------------
    RtEvent CollectiveView::find_collective_analyses(size_t context_index,
              unsigned index, const std::vector<CollectiveAnalysis*> *&analyses)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(!local_views.empty());
      assert(collective_mapping != NULL);
#endif
      const RendezvousKey key(context_index, index);
      AutoLock v_lock(view_lock);
      std::map<RendezvousKey,UserRendezvous>::iterator finder =
        rendezvous_users.find(key);
      if (finder == rendezvous_users.end())
      {
        finder = rendezvous_users.insert(
            std::make_pair(key,UserRendezvous())).first; 
        UserRendezvous &rendezvous = finder->second;
        rendezvous.local_initialized = false;
        rendezvous.remaining_remote_arrivals =
          collective_mapping->count_children(owner_space, local_space);
        rendezvous.local_registered = Runtime::create_rt_user_event();
        rendezvous.global_registered = Runtime::create_rt_user_event();
      }
      analyses = &finder->second.analyses;
      if ((finder->second.analyses.empty() || 
            (finder->second.remaining_analyses > 0)) &&
          !finder->second.analyses_ready.exists())
        finder->second.analyses_ready = Runtime::create_rt_user_event();
      return finder->second.analyses_ready;
    }

    //--------------------------------------------------------------------------
    ApEvent CollectiveView::register_collective_user(const RegionUsage &usage,
                                         const FieldMask &user_mask,
                                         IndexSpaceNode *expr,
                                         const UniqueID op_id,
                                         const size_t op_ctx_index,
                                         const unsigned index,
                                         ApEvent term_event,
                                         RtEvent collect_event,
                                         PhysicalManager *target,
                                         size_t local_collective_arrivals,
                                         std::set<RtEvent> &applied_events,
                                         const PhysicalTraceInfo &trace_info,
                                         const bool symbolic)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(!local_views.empty());
      assert(((collective_mapping != NULL) && 
            collective_mapping->contains(local_space)) || is_owner());
#endif
      const unsigned target_index = find_local_index(target);
      // We performing a collective analysis, this function performs a 
      // parallel rendezvous to ensure several important invariants.
      // 1. SUBTLE!!! Make sure that all the participants have arrived
      //    at this function before performing any view analysis. This
      //    is required to ensure that any copies that need to be issued
      //    have had a chance to record their view users first before we
      //    attempt to look for preconditions for this user.
      // 2. Similarly make sure that the applied events reflects the case
      //    where all the users have been recorded across the views on 
      //    each node to ensure that any downstream copies or users will
      //    observe all the most recent users.
      // 3. Deduplicate across all the participants on the same node since
      //    there is always just a single view on each node. This function
      //    call will always return the local user precondition for the
      //    local instances. Make sure to merge together all the partcipant
      //    postconditions for the local instances to reflect in the view
      //    that the local instances are ready when they are all ready.
      // 4. Do NOT block in this function call or you can risk deadlock because
      //    we might be doing several of these calls for a region requirement
      //    on different instances and the orders might vary on each node.
      
      // The unique tag for the rendezvous is our context ID which will be
      // the same across all points and the index of our region requirement
      PhysicalTraceInfo *result_info;
      RtUserEvent local_registered, global_registered;
      std::vector<RtEvent> remote_registered;
      std::vector<ApUserEvent> local_ready_events;
      std::vector<std::vector<ApEvent> > local_term_events;
      std::vector<CollectiveAnalysis*> analyses;
      const RendezvousKey key(op_ctx_index, index);
      {
        AutoLock v_lock(view_lock);
        // Check to see if we're the first one to arrive on this node
        std::map<RendezvousKey,UserRendezvous>::iterator finder =
          rendezvous_users.find(key);
        if (finder == rendezvous_users.end())
        {
          // If we are then make the record for knowing when we've seen
          // all the expected arrivals
          finder = rendezvous_users.insert(
              std::make_pair(key,UserRendezvous())).first; 
          UserRendezvous &rendezvous = finder->second;
          // Count how many expected arrivals we have
          // If we're doing collective per space 
          rendezvous.remaining_local_arrivals = local_collective_arrivals;
          rendezvous.local_initialized = true;
          rendezvous.remaining_remote_arrivals =
            (collective_mapping == NULL) ? 0 :
            collective_mapping->count_children(owner_space, local_space);
          rendezvous.local_term_events.resize(local_views.size());
          rendezvous.ready_events.resize(local_views.size());
          for (unsigned idx = 0; idx < local_views.size(); idx++)
            rendezvous.ready_events[idx] =
              Runtime::create_ap_user_event(&trace_info);
          rendezvous.trace_info = new PhysicalTraceInfo(trace_info);
          rendezvous.local_registered = Runtime::create_rt_user_event();
          rendezvous.global_registered = Runtime::create_rt_user_event();
        }
        else if (!finder->second.local_initialized)
        {
          // First local arrival, but rendezvous was made by a remote
          // arrival so we need to make the ready event
#ifdef DEBUG_LEGION
          assert(finder->second.ready_events.empty());
          assert(finder->second.local_term_events.empty());
          assert(finder->second.trace_info == NULL);
#endif
          finder->second.local_term_events.resize(local_views.size());
          finder->second.ready_events.resize(local_views.size());
          for (unsigned idx = 0; idx < local_views.size(); idx++)
            finder->second.ready_events[idx] =
              Runtime::create_ap_user_event(&trace_info);
          finder->second.trace_info = new PhysicalTraceInfo(trace_info);
          finder->second.remaining_local_arrivals = local_collective_arrivals;
          finder->second.local_initialized = true;
        } 
        if (term_event.exists())
          finder->second.local_term_events[target_index].push_back(term_event);
        // Record the applied events
        applied_events.insert(finder->second.global_registered);
        // The result will be the ready event
        ApEvent result = finder->second.ready_events[target_index];
        result_info = finder->second.trace_info;
#ifdef DEBUG_LEGION
        assert(finder->second.local_initialized);
        assert(finder->second.remaining_local_arrivals > 0);
#endif
        // See if we've seen all the arrivals
        if (--finder->second.remaining_local_arrivals == 0)
        {
          // If we're going to need to defer this then save
          // all of our local state needed to perform registration
          // for when it is safe to do so
          if (!is_owner() || 
              (finder->second.remaining_remote_arrivals > 0))
          {
            // Save the state that we need for finalization later
            finder->second.usage = usage;
            finder->second.mask = new FieldMask(user_mask);
            finder->second.expr = expr;
            WrapperReferenceMutator mutator(applied_events);
            expr->add_nested_expression_reference(did, &mutator);
            finder->second.op_id = op_id;
            finder->second.collect_event = collect_event;
            finder->second.symbolic = symbolic;
          }
          if (finder->second.remaining_remote_arrivals == 0)
          {
            if (!is_owner())
            {
              // Not the owner so send the message to the parent
              RtEvent registered = finder->second.local_registered;
              if (!finder->second.remote_registered.empty())
              {
                finder->second.remote_registered.push_back(registered);
                registered =
                  Runtime::merge_events(finder->second.remote_registered);
              }
              const AddressSpaceID parent = 
                collective_mapping->get_parent(owner_space, local_space);
              Serializer rez;
              {
                RezCheck z(rez);
                rez.serialize(did);
                rez.serialize(op_ctx_index);
                rez.serialize(index);
                rez.serialize(registered);
              }
              runtime->send_collective_register_user_request(parent, rez);
              return result;
            }
            else
            {
#ifdef DEBUG_LEGION
              assert(finder->second.remaining_analyses == 0);
#endif
              // We're going to fall through so grab the state
              // that we need to do the finalization now
              remote_registered.swap(finder->second.remote_registered);
              local_registered = finder->second.local_registered;
              global_registered = finder->second.global_registered;
              local_ready_events.swap(finder->second.ready_events);
              local_term_events.swap(finder->second.local_term_events);
              analyses.swap(finder->second.analyses);
              // We can erase this from the data structure now
              rendezvous_users.erase(finder);
            }
          }
          else // Still waiting for remote arrivals
            return result;
        }
        else // Not the last local arrival so we can just return the result
          return result;
      }
#ifdef DEBUG_LEGION
      assert(is_owner());
#endif
      finalize_collective_user(usage, user_mask, expr, op_id, op_ctx_index, 
          index, collect_event, local_registered, global_registered,
          local_ready_events, local_term_events, result_info, 
          analyses, symbolic);
      RtEvent all_registered = local_registered;
      if (!remote_registered.empty())
      {
        remote_registered.push_back(all_registered);
        all_registered = Runtime::merge_events(remote_registered);
      }
      Runtime::trigger_event(global_registered, all_registered); 
      return local_ready_events[target_index];
    }

    //--------------------------------------------------------------------------
    void CollectiveView::process_register_user_request(
            const size_t op_ctx_index, const unsigned index, RtEvent registered)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(!local_views.empty());
#endif
      UserRendezvous to_perform;
      const RendezvousKey key(op_ctx_index, index);
      {
        AutoLock v_lock(view_lock);
        // Check to see if we're the first one to arrive on this node
        std::map<RendezvousKey,UserRendezvous>::iterator
          finder = rendezvous_users.find(key);
        if (finder == rendezvous_users.end())
        {
          // If we are then make the record for knowing when we've seen
          // all the expected arrivals
          finder = rendezvous_users.insert(
              std::make_pair(key,UserRendezvous())).first; 
          UserRendezvous &rendezvous = finder->second;
          rendezvous.local_initialized = false;
          rendezvous.remaining_remote_arrivals =
            collective_mapping->count_children(owner_space, local_space);
          rendezvous.local_registered = Runtime::create_rt_user_event();
          rendezvous.global_registered = Runtime::create_rt_user_event();
        }
        finder->second.remote_registered.push_back(registered);
#ifdef DEBUG_LEGION
        assert(finder->second.remaining_remote_arrivals > 0);
#endif
        // If we're not the last arrival then we're done
        if ((--finder->second.remaining_remote_arrivals > 0) ||
            !finder->second.local_initialized ||
            (finder->second.remaining_local_arrivals > 0))
          return;
        if (!is_owner())
        {
          // Continue sending the message up the tree to the parent
          registered = finder->second.local_registered;
          if (!finder->second.remote_registered.empty())
          {
            finder->second.remote_registered.push_back(registered);
            registered =
              Runtime::merge_events(finder->second.remote_registered);
          }
          const AddressSpaceID parent = 
            collective_mapping->get_parent(owner_space, local_space);
          Serializer rez;
          {
            RezCheck z(rez);
            rez.serialize(did);
            rez.serialize(op_ctx_index);
            rez.serialize(index);
            rez.serialize(registered);
          }
          runtime->send_collective_register_user_request(parent, rez);
          return;
        }
#ifdef DEBUG_LEGION
        assert(finder->second.remaining_analyses == 0);
#endif
        // We're the owner so we can start doing the user registration
        // Grab everything we need to call finalize_collective_user
        to_perform = std::move(finder->second);
        // Then we can erase the entry
        rendezvous_users.erase(finder);
      }
#ifdef DEBUG_LEGION
      assert(is_owner());
#endif
      finalize_collective_user(to_perform.usage, *(to_perform.mask),
          to_perform.expr, to_perform.op_id, op_ctx_index, index, 
          to_perform.collect_event, to_perform.local_registered,
          to_perform.global_registered, to_perform.ready_events, 
          to_perform.local_term_events, to_perform.trace_info,
          to_perform.analyses, to_perform.symbolic);
      RtEvent all_registered = to_perform.local_registered;
      if (!to_perform.remote_registered.empty())
      {
        to_perform.remote_registered.push_back(all_registered);
        all_registered = Runtime::merge_events(to_perform.remote_registered);
      }
      Runtime::trigger_event(to_perform.global_registered, all_registered);
      if (to_perform.expr->remove_nested_expression_reference(did))
        delete to_perform.expr;
      delete to_perform.mask;
    }

    //--------------------------------------------------------------------------
    /*static*/ void CollectiveView::handle_register_user_request(
                                          Runtime *runtime, Deserializer &derez)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez);
      DistributedID did;
      derez.deserialize(did);
      RtEvent ready;
      CollectiveView *view = static_cast<CollectiveView*>(
              runtime->find_or_request_logical_view(did, ready));
      size_t op_ctx_index;
      derez.deserialize(op_ctx_index);
      unsigned index;
      derez.deserialize(index);
      RtEvent registered;
      derez.deserialize(registered);

      if (ready.exists() && !ready.has_triggered())
        ready.wait();
      view->process_register_user_request(op_ctx_index, index, registered);
    }

    //--------------------------------------------------------------------------
    void CollectiveView::process_register_user_response(
            const size_t op_ctx_index, const unsigned index, RtEvent registered)
    //--------------------------------------------------------------------------
    {
#ifdef DEBUG_LEGION
      assert(!is_owner());
      assert(!local_views.empty());
#endif
      UserRendezvous to_perform;
      const RendezvousKey key(op_ctx_index, index);
      {
        AutoLock v_lock(view_lock);
        // Check to see if we're the first one to arrive on this node
        std::map<RendezvousKey,UserRendezvous>::iterator finder =
          rendezvous_users.find(key);
#ifdef DEBUG_LEGION
        assert(finder != rendezvous_users.end());
        assert(finder->second.remaining_analyses == 0);
#endif
        to_perform = std::move(finder->second);
        // Can now remove this from the data structure
        rendezvous_users.erase(finder);
      }
      // Now we can perform the user registration
      finalize_collective_user(to_perform.usage, *(to_perform.mask),
          to_perform.expr, to_perform.op_id, op_ctx_index, index,
          to_perform.collect_event, to_perform.local_registered, 
          to_perform.global_registered, to_perform.ready_events,
          to_perform.local_term_events, to_perform.trace_info,
          to_perform.analyses, to_perform.symbolic);
      Runtime::trigger_event(to_perform.global_registered, registered);
      if (to_perform.expr->remove_nested_expression_reference(did))
        delete to_perform.expr;
      delete to_perform.mask;
    }

    //--------------------------------------------------------------------------
    /*static*/ void CollectiveView::handle_register_user_response(
                                          Runtime *runtime, Deserializer &derez)
    //--------------------------------------------------------------------------
    {
      DerezCheck z(derez);
      DistributedID did;
      derez.deserialize(did);
      RtEvent ready;
      CollectiveView *view = static_cast<CollectiveView*>(
              runtime->find_or_request_logical_view(did, ready));
      size_t op_ctx_index;
      derez.deserialize(op_ctx_index);
      unsigned index;
      derez.deserialize(index);
      RtEvent registered;
      derez.deserialize(registered);

      if (ready.exists() && !ready.has_triggered())
        ready.wait();
      view->process_register_user_response(op_ctx_index, index, registered);
    }

    //--------------------------------------------------------------------------
    void CollectiveView::finalize_collective_user(
                                const RegionUsage &usage,
                                const FieldMask &user_mask,
                                IndexSpaceNode *expr,
                                const UniqueID op_id,
                                const size_t op_ctx_index,
                                const unsigned index,
                                RtEvent collect_event,
                                RtUserEvent local_registered,
                                RtEvent global_registered,
                                std::vector<ApUserEvent> &ready_events,
                                std::vector<std::vector<ApEvent> > &term_events,
                                const PhysicalTraceInfo *trace_info,
                                std::vector<CollectiveAnalysis*> &analyses,
                                const bool symbolic) const
    //--------------------------------------------------------------------------
    {
      // First send out any messages to the children so they can start
      // their own registrations
      std::vector<AddressSpaceID> children;
      collective_mapping->get_children(owner_space, local_space, children);
      if (!children.empty())
      {
        Serializer rez;
        {
          RezCheck z(rez);
          rez.serialize(did);
          rez.serialize(op_ctx_index);
          rez.serialize(index);
          rez.serialize(global_registered);
        }
        for (std::vector<AddressSpaceID>::const_iterator it =
              children.begin(); it != children.end(); it++)
          runtime->send_collective_register_user_response(*it, rez);
      }
#ifdef DEBUG_LEGION
      assert(local_views.size() == term_events.size());
      assert(local_views.size() == ready_events.size());
#endif
      // Perform the registration on the local views
      std::set<RtEvent> registered_events;
      for (unsigned idx = 0; idx < local_views.size(); idx++)
      {
        const ApEvent term_event = 
          Runtime::merge_events(trace_info, term_events[idx]);
        const ApEvent ready = local_views[idx]->register_user(usage, user_mask,
            expr, op_id, op_ctx_index, index, term_event, collect_event,
            local_views[idx]->get_manager(), 0/*no collective arrivals*/,
            registered_events, *trace_info, runtime->address_space, symbolic);
        Runtime::trigger_event(trace_info, ready_events[idx], ready);
      }
      if (!registered_events.empty())
        Runtime::trigger_event(local_registered,
            Runtime::merge_events(registered_events));
      else
        Runtime::trigger_event(local_registered);
      // Remove any references on the analyses
      for (std::vector<CollectiveAnalysis*>::const_iterator it =
            analyses.begin(); it != analyses.end(); it++)
        if ((*it)->remove_analysis_reference())
          delete (*it);
      delete trace_info;
    }

    /////////////////////////////////////////////////////////////
    // ReplicatedView
    /////////////////////////////////////////////////////////////

    //--------------------------------------------------------------------------
    ReplicatedView::ReplicatedView(RegionTreeForest *ctx, DistributedID id,
                                   AddressSpaceID owner_proc, 
                                   UniqueID owner_context, 
                                   const std::vector<IndividualView*> &views,
                                   bool register_now,CollectiveMapping *mapping)
      : CollectiveView(ctx, id, owner_proc, owner_context, views, 
                       register_now, mapping)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    ReplicatedView::~ReplicatedView(void)
    //--------------------------------------------------------------------------
    {
    }

    /////////////////////////////////////////////////////////////
    // AllreduceView
    /////////////////////////////////////////////////////////////

    //--------------------------------------------------------------------------
    AllreduceView::AllreduceView(RegionTreeForest *ctx, DistributedID id,
                                 AddressSpaceID owner_proc, 
                                 UniqueID owner_context, 
                                 const std::vector<IndividualView*> &views,
                                 bool register_now, CollectiveMapping *mapping,
                                 ReductionOpID redop_id)
      : CollectiveView(ctx, id, owner_proc, owner_context, views, 
                       register_now, mapping), redop(redop_id)
    //--------------------------------------------------------------------------
    {
    }

    //--------------------------------------------------------------------------
    AllreduceView::~AllreduceView(void)
    //--------------------------------------------------------------------------
    {
    }

  }; // namespace Internal 
}; // namespace Legion

