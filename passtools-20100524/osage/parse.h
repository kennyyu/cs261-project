#ifndef PARSE_H_1182556825
#define PARSE_H_1182556825

typedef union {
  long alignment;
  char ag_vt_2[sizeof(int)];
  char ag_vt_4[sizeof(ptnode *)];
  char ag_vt_5[sizeof(char *)];
} parse_vs_type;

typedef enum {
  parse_query_token = 6, parse_ws_token, parse_expr_token = 9,
  parse_eof_token = 12, parse_bind_expr_token, parse_lambda_expr_token,
  parse_filter_expr_token, parse_for_head_token, parse_let_head_token,
  parse_lambda_head_token, parse_binding_name_token = 21,
  parse_ident_token = 23, parse_tuple_expr_token = 25,
  parse_real_tuple_expr_token, parse_path_expr_token,
  parse_qualified_path_element_token = 29, parse_path_element_token = 33,
  parse_value_expr_token = 35, parse_logical_expr_token = 37,
  parse_equality_expr_token, parse_comparison_expr_token = 41,
  parse_cons_token = 44, parse_term_token = 51,
  parse_range_start_token = 53, parse_number_token, parse_factor_token = 56,
  parse_suffix_expr_token = 60, parse_prefix_expr_token = 64,
  parse_base_expr_token = 68, parse_string_token,
  parse_string_body_token = 79, parse_string_char_token,
  parse_hex_digit_token = 84, parse_letter_token, parse_digit_token = 87,
  parse_ws_char_token = 90
} parse_token_type;

typedef struct parse_pcb_struct{
  parse_token_type token_number, reduction_token, error_frame_token;
  int input_code;
  int input_value;
  int line, column;
  int ssx, sn, error_frame_ssx;
  int drt, dssx, dsn;
  int ss[128];
  parse_vs_type vs[128];
  int ag_ap;
  const char *error_message;
  char read_flag;
  char exit_flag;
  int bts[128], btsx;
  int (* const  *gt_procs)(void);
  int (* const *r_procs)(void);
  int (* const *s_procs)(void);
  int lab[9], rx, fx;
  const unsigned char *key_sp;
  int save_index, key_state;
  int ag_error_depth, ag_min_depth, ag_tmp_depth;
  int ag_rss[2*128], ag_lrss;
  int ag_rk1, ag_tk1;
  char ag_msg[82];
  int ag_resynch_active;
} parse_pcb_type;

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

extern parse_pcb_type parse_pcb;
void init_parse(void);
void parse(void);
#endif

