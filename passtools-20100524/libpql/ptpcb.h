#ifndef PTPARSE_H_1270595738
#define PTPARSE_H_1270595738

typedef union {
  long alignment;
  char ag_vt_4[sizeof(struct tokstr)];
  char ag_vt_5[sizeof(struct ptexpr *)];
  char ag_vt_6[sizeof(struct ptcolumnvararray *)];
  char ag_vt_7[sizeof(bool)];
  char ag_vt_8[sizeof(struct ptexprarray *)];
  char ag_vt_9[sizeof(enum functions)];
  char ag_vt_10[sizeof(struct ptpath *)];
  char ag_vt_11[sizeof(struct ptcolumnvar *)];
} ptparse_vs_type;

typedef enum {
  ptparse_complete_query_token = 1, ptparse_query_token,
  ptparse_SEMIC_token, ptparse_eof_token = 5, ptparse_nonsfw_query_token,
  ptparse_sfw_query_token, ptparse_SELECT_token, ptparse_distinct_token,
  ptparse_selection_list_token, ptparse_sfw_body_token,
  ptparse_from_list_token, ptparse_WHERE_token,
  ptparse_quantified_expression_token, ptparse_HAVING_token,
  ptparse_GROUP_token, ptparse_BY_token, ptparse_columnvar_list_token,
  ptparse_WITH_token, ptparse_columnvar_token, ptparse_UNGROUP_token,
  ptparse_COMMA_token, ptparse_DISTINCT_token, ptparse_selection_token,
  ptparse_AS_token, ptparse_IDENTIFIER_token, ptparse_unquote_token,
  ptparse_COLON_token, ptparse_FROM_token, ptparse_from_item_list_token,
  ptparse_from_item_token, ptparse_path_token, ptparse_IN_token = 34,
  ptparse_nested_sfw_token, ptparse_LPAREN_token, ptparse_RPAREN_token,
  ptparse_logical_expression_token, ptparse_EXISTS_token, ptparse_FOR_token,
  ptparse_ALL_token, ptparse_element_expression_token,
  ptparse_logical_op_token, ptparse_AND_token, ptparse_OR_token,
  ptparse_set_expression_token, ptparse_comparison_expression_token,
  ptparse_set_op_token, ptparse_INTERSECT_token, ptparse_UNION_token,
  ptparse_EXCEPT_token, ptparse_path_or_term_token,
  ptparse_comparison_op_token, ptparse_EQ_token = 55, ptparse_LTGT_token,
  ptparse_LT_token, ptparse_LTEQ_token, ptparse_GTEQ_token,
  ptparse_GT_token, ptparse_LIKE_token, ptparse_GLOB_token,
  ptparse_GREP_token, ptparse_SOUNDEX_token, ptparse_term_token,
  ptparse_factor_token, ptparse_add_op_token, ptparse_PLUS_token,
  ptparse_MINUS_token, ptparse_PLUSPLUS_token,
  ptparse_prefix_expression_token, ptparse_mul_op_token, ptparse_STAR_token,
  ptparse_SLASH_token, ptparse_MOD_token, ptparse_suffix_expression_token,
  ptparse_NOT_token, ptparse_base_expression_token, ptparse_PATHOF_token,
  ptparse_anyvar_token, ptparse_func_token, ptparse_function_name_token,
  ptparse_query_list_token, ptparse_NEW_token, ptparse_constant_token,
  ptparse_MIN_token, ptparse_MAX_token, ptparse_COUNT_token,
  ptparse_SUM_token, ptparse_AVG_token, ptparse_ABS_token,
  ptparse_ELEMENT_token, ptparse_SET_token, ptparse_FUNCNAME_token,
  ptparse_NIL_token, ptparse_INTEGER_LITERAL_token,
  ptparse_REAL_LITERAL_token, ptparse_QUOTED_STRING_LITERAL_token,
  ptparse_TRUE_token, ptparse_FALSE_token,
  ptparse_top_sequential_path_token, ptparse_sequential_path_token,
  ptparse_alternative_path_token, ptparse_varbinding_path_token,
  ptparse_PIPE_token, ptparse_pathbinding_path_token, ptparse_LBRACE_token,
  ptparse_RBRACE_token, ptparse_repetition_path_token, ptparse_AT_token,
  ptparse_base_path_token, ptparse_QUES_token, ptparse_DOT_token,
  ptparse_label_token, ptparse_HASH_token, ptparse_OF_token,
  ptparse_UNQUOTE_token, ptparse_LBRACKBRACK_token,
  ptparse_RBRACKBRACK_token
} ptparse_token_type;

typedef struct ptparse_pcb_struct{
  ptparse_token_type token_number, reduction_token, error_frame_token;
  int input_code;
  struct tokstr input_value;
  int line, column;
  int ssx, sn, error_frame_ssx;
  int drt, dssx, dsn;
  int ss[128];
  ptparse_vs_type vs[128];
  int ag_ap;
  const char *error_message;
  char read_flag;
  char exit_flag;
  int bts[128], btsx;
  int (* const *gt_procs)(struct ptparse_pcb_struct *);
  int (* const *r_procs)(struct ptparse_pcb_struct *);
  int (* const *s_procs)(struct ptparse_pcb_struct *);
  int ag_error_depth, ag_min_depth, ag_tmp_depth;
  int ag_rss[2*128], ag_lrss;
  int ag_rk1, ag_tk1;
  char ag_msg[82];
  int ag_resynch_active;
/*  Line 65, /home/vfiler/dholland/projects/pql/hg/rehacking/libpql/ptparse.syn */
     struct pqlcontext *pql;
     struct ptexpr *result;
     bool failed;
  } ptparse_pcb_type;

#ifndef PRULE_CONTEXT
#define PRULE_CONTEXT(pcb)  (&((pcb).cs[(pcb).ssx]))
#define PERROR_CONTEXT(pcb) ((pcb).cs[(pcb).error_frame_ssx])
#define PCONTEXT(pcb)       ((pcb).cs[(pcb).ssx])
#endif

#ifndef AG_RUNNING_CODE
/* PCB.exit_flag values */
#define AG_RUNNING_CODE         0
#define AG_SUCCESS_CODE         1
#define AG_SYNTAX_ERROR_CODE    2
#define AG_REDUCTION_ERROR_CODE 3
#define AG_STACK_ERROR_CODE     4
#define AG_SEMANTIC_ERROR_CODE  5
#endif
void init_ptparse(ptparse_pcb_type *);
void ptparse(ptparse_pcb_type *);
#endif

