#ifndef BETTERSH_AST_H
#define BETTERSH_AST_H

#include <stdint.h>
#include <stdio.h>

/* Syntax tree for strict shell programs.
   Milestone 1 only builds the sh command nodes below. */

typedef enum PartKind {
    PART_LIT = 1,
    PART_PARAM,
    PART_CMDSUB,
    PART_ARITH
} PartKind;

typedef enum ParamOp {
    P_NONE = 0,
    P_LEN,
    P_MINUS,
    P_COLON_MINUS,
    P_EQ,
    P_COLON_EQ,
    P_PLUS,
    P_COLON_PLUS,
    P_QUEST,
    P_COLON_QUEST,
    P_HASH,
    P_HASH_HASH,
    P_PCT,
    P_PCT_PCT,
    /* ${name:}word} — the value, or empty when unset or null, then word. */
    P_COLON_CONCAT
} ParamOp;

typedef enum NodeKind {
    NODE_PROGRAM = 1,
    NODE_AND_OR,
    NODE_PIPELINE,
    NODE_SIMPLE,
    NODE_BRACE,
    NODE_SUBSHELL,
    NODE_IF,
    NODE_WHILE,
    NODE_FOR,
    NODE_CASE,
    NODE_FUNC
} NodeKind;

typedef enum SepKind {
    SEP_NONE = 0,
    SEP_SEMI,
    SEP_AMP,
    SEP_NL
} SepKind;

typedef enum AndOrOp {
    ANDOR_AND = 1,
    ANDOR_OR
} AndOrOp;

typedef enum RedirOp {
    REDIR_IN = 1,
    REDIR_OUT,
    REDIR_APPEND,
    REDIR_IN_DUP,
    REDIR_OUT_DUP,
    REDIR_RW,
    REDIR_CLOBBER,
    REDIR_HERE,
    REDIR_HERE_DASH,
    REDIR_HERESTR
} RedirOp;

typedef struct WordPart {
    PartKind kind;
    int line;
    int col;
    int quoted;
    char *text;
    ParamOp op;
    struct Word *nested;
    struct Node *child;
} WordPart;

typedef struct Word {
    WordPart *parts;
    uint32_t nparts;
    int line;
    int col;
    int quoted;
    int is_assign;
    char *assign_name;
    char *spell;
} Word;

typedef struct Redirect {
    RedirOp op;
    int fd;
    int line;
    int col;
    int noexpand;
    Word *word;
    Word *heredoc;
} Redirect;

typedef struct Node Node;

typedef struct Item {
    Node *node;
    SepKind sep;
} Item;

typedef struct CaseArm {
    Word **pats;
    uint32_t npats;
    Node *body;
} CaseArm;

struct Node {
    NodeKind kind;
    int line;
    int col;
    /* Last source line of a compound, so set -v can print through fi/done/esac
       before the body runs. 0 means the same line as line. */
    int end_line;
    union {
        struct {
            Item *items;
            uint32_t n;
        } program;
        struct {
            AndOrOp op;
            Node *left;
            Node *right;
        } andor;
        struct {
            Node **cmds;
            uint32_t n;
            int bang;
        } pipeline;
        struct {
            Word **assigns;
            uint32_t nassigns;
            Word **argv;
            uint32_t nargv;
            Redirect **redirs;
            uint32_t nredirs;
        } simple;
        struct {
            Node *body;
            Redirect **redirs;
            uint32_t nredirs;
        } group;
        struct {
            Node *cond;
            Node *body;
            Node *otherwise;
            Redirect **redirs;
            uint32_t nredirs;
        } ifc;
        struct {
            Node *cond;
            Node *body;
            int until;
            Redirect **redirs;
            uint32_t nredirs;
        } loop;
        struct {
            char *name;
            Word **words;
            uint32_t nwords;
            int has_in;
            Node *body;
            Redirect **redirs;
            uint32_t nredirs;
        } forloop;
        struct {
            Word *word;
            CaseArm *arms;
            uint32_t narms;
            Redirect **redirs;
            uint32_t nredirs;
        } cas;
        struct {
            char *name;
            Node *body;
        } func;
    } u;
};

void ast_dump(const Node *node, FILE *out);

#endif
