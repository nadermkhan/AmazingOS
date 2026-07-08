.code64
.section .text
.extern interrupt_handler

# --- Common ISR Stub Handler ---
isr_common_stub:
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, %rdi
    call interrupt_handler
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
    addq $16, %rsp
    iretq

.global isr0
isr0:
    pushq $0
    pushq $0
    jmp isr_common_stub

.global isr1
isr1:
    pushq $0
    pushq $1
    jmp isr_common_stub

.global isr2
isr2:
    pushq $0
    pushq $2
    jmp isr_common_stub

.global isr3
isr3:
    pushq $0
    pushq $3
    jmp isr_common_stub

.global isr4
isr4:
    pushq $0
    pushq $4
    jmp isr_common_stub

.global isr5
isr5:
    pushq $0
    pushq $5
    jmp isr_common_stub

.global isr6
isr6:
    pushq $0
    pushq $6
    jmp isr_common_stub

.global isr7
isr7:
    pushq $0
    pushq $7
    jmp isr_common_stub

.global isr8
isr8:
    pushq $8
    jmp isr_common_stub

.global isr9
isr9:
    pushq $0
    pushq $9
    jmp isr_common_stub

.global isr10
isr10:
    pushq $10
    jmp isr_common_stub

.global isr11
isr11:
    pushq $11
    jmp isr_common_stub

.global isr12
isr12:
    pushq $12
    jmp isr_common_stub

.global isr13
isr13:
    pushq $13
    jmp isr_common_stub

.global isr14
isr14:
    pushq $14
    jmp isr_common_stub

.global isr15
isr15:
    pushq $0
    pushq $15
    jmp isr_common_stub

.global isr16
isr16:
    pushq $0
    pushq $16
    jmp isr_common_stub

.global isr17
isr17:
    pushq $17
    jmp isr_common_stub

.global isr18
isr18:
    pushq $0
    pushq $18
    jmp isr_common_stub

.global isr19
isr19:
    pushq $0
    pushq $19
    jmp isr_common_stub

.global isr20
isr20:
    pushq $0
    pushq $20
    jmp isr_common_stub

.global isr21
isr21:
    pushq $21
    jmp isr_common_stub

.global isr22
isr22:
    pushq $0
    pushq $22
    jmp isr_common_stub

.global isr23
isr23:
    pushq $0
    pushq $23
    jmp isr_common_stub

.global isr24
isr24:
    pushq $0
    pushq $24
    jmp isr_common_stub

.global isr25
isr25:
    pushq $0
    pushq $25
    jmp isr_common_stub

.global isr26
isr26:
    pushq $0
    pushq $26
    jmp isr_common_stub

.global isr27
isr27:
    pushq $0
    pushq $27
    jmp isr_common_stub

.global isr28
isr28:
    pushq $0
    pushq $28
    jmp isr_common_stub

.global isr29
isr29:
    pushq $29
    jmp isr_common_stub

.global isr30
isr30:
    pushq $30
    jmp isr_common_stub

.global isr31
isr31:
    pushq $0
    pushq $31
    jmp isr_common_stub

.global isr32
isr32:
    pushq $0
    pushq $32
    jmp isr_common_stub

.global isr33
isr33:
    pushq $0
    pushq $33
    jmp isr_common_stub

.global isr34
isr34:
    pushq $0
    pushq $34
    jmp isr_common_stub

.global isr35
isr35:
    pushq $0
    pushq $35
    jmp isr_common_stub

.global isr36
isr36:
    pushq $0
    pushq $36
    jmp isr_common_stub

.global isr37
isr37:
    pushq $0
    pushq $37
    jmp isr_common_stub

.global isr38
isr38:
    pushq $0
    pushq $38
    jmp isr_common_stub

.global isr39
isr39:
    pushq $0
    pushq $39
    jmp isr_common_stub

.global isr40
isr40:
    pushq $0
    pushq $40
    jmp isr_common_stub

.global isr41
isr41:
    pushq $0
    pushq $41
    jmp isr_common_stub

.global isr42
isr42:
    pushq $0
    pushq $42
    jmp isr_common_stub

.global isr43
isr43:
    pushq $0
    pushq $43
    jmp isr_common_stub

.global isr44
isr44:
    pushq $0
    pushq $44
    jmp isr_common_stub

.global isr45
isr45:
    pushq $0
    pushq $45
    jmp isr_common_stub

.global isr46
isr46:
    pushq $0
    pushq $46
    jmp isr_common_stub

.global isr47
isr47:
    pushq $0
    pushq $47
    jmp isr_common_stub

.global isr48
isr48:
    pushq $0
    pushq $48
    jmp isr_common_stub

.global isr49
isr49:
    pushq $0
    pushq $49
    jmp isr_common_stub

.global isr50
isr50:
    pushq $0
    pushq $50
    jmp isr_common_stub

.global isr51
isr51:
    pushq $0
    pushq $51
    jmp isr_common_stub

.global isr52
isr52:
    pushq $0
    pushq $52
    jmp isr_common_stub

.global isr53
isr53:
    pushq $0
    pushq $53
    jmp isr_common_stub

.global isr54
isr54:
    pushq $0
    pushq $54
    jmp isr_common_stub

.global isr55
isr55:
    pushq $0
    pushq $55
    jmp isr_common_stub

.global isr56
isr56:
    pushq $0
    pushq $56
    jmp isr_common_stub

.global isr57
isr57:
    pushq $0
    pushq $57
    jmp isr_common_stub

.global isr58
isr58:
    pushq $0
    pushq $58
    jmp isr_common_stub

.global isr59
isr59:
    pushq $0
    pushq $59
    jmp isr_common_stub

.global isr60
isr60:
    pushq $0
    pushq $60
    jmp isr_common_stub

.global isr61
isr61:
    pushq $0
    pushq $61
    jmp isr_common_stub

.global isr62
isr62:
    pushq $0
    pushq $62
    jmp isr_common_stub

.global isr63
isr63:
    pushq $0
    pushq $63
    jmp isr_common_stub

.global isr64
isr64:
    pushq $0
    pushq $64
    jmp isr_common_stub

.global isr65
isr65:
    pushq $0
    pushq $65
    jmp isr_common_stub

.global isr66
isr66:
    pushq $0
    pushq $66
    jmp isr_common_stub

.global isr67
isr67:
    pushq $0
    pushq $67
    jmp isr_common_stub

.global isr68
isr68:
    pushq $0
    pushq $68
    jmp isr_common_stub

.global isr69
isr69:
    pushq $0
    pushq $69
    jmp isr_common_stub

.global isr70
isr70:
    pushq $0
    pushq $70
    jmp isr_common_stub

.global isr71
isr71:
    pushq $0
    pushq $71
    jmp isr_common_stub

.global isr72
isr72:
    pushq $0
    pushq $72
    jmp isr_common_stub

.global isr73
isr73:
    pushq $0
    pushq $73
    jmp isr_common_stub

.global isr74
isr74:
    pushq $0
    pushq $74
    jmp isr_common_stub

.global isr75
isr75:
    pushq $0
    pushq $75
    jmp isr_common_stub

.global isr76
isr76:
    pushq $0
    pushq $76
    jmp isr_common_stub

.global isr77
isr77:
    pushq $0
    pushq $77
    jmp isr_common_stub

.global isr78
isr78:
    pushq $0
    pushq $78
    jmp isr_common_stub

.global isr79
isr79:
    pushq $0
    pushq $79
    jmp isr_common_stub

.global isr80
isr80:
    pushq $0
    pushq $80
    jmp isr_common_stub

.global isr81
isr81:
    pushq $0
    pushq $81
    jmp isr_common_stub

.global isr82
isr82:
    pushq $0
    pushq $82
    jmp isr_common_stub

.global isr83
isr83:
    pushq $0
    pushq $83
    jmp isr_common_stub

.global isr84
isr84:
    pushq $0
    pushq $84
    jmp isr_common_stub

.global isr85
isr85:
    pushq $0
    pushq $85
    jmp isr_common_stub

.global isr86
isr86:
    pushq $0
    pushq $86
    jmp isr_common_stub

.global isr87
isr87:
    pushq $0
    pushq $87
    jmp isr_common_stub

.global isr88
isr88:
    pushq $0
    pushq $88
    jmp isr_common_stub

.global isr89
isr89:
    pushq $0
    pushq $89
    jmp isr_common_stub

.global isr90
isr90:
    pushq $0
    pushq $90
    jmp isr_common_stub

.global isr91
isr91:
    pushq $0
    pushq $91
    jmp isr_common_stub

.global isr92
isr92:
    pushq $0
    pushq $92
    jmp isr_common_stub

.global isr93
isr93:
    pushq $0
    pushq $93
    jmp isr_common_stub

.global isr94
isr94:
    pushq $0
    pushq $94
    jmp isr_common_stub

.global isr95
isr95:
    pushq $0
    pushq $95
    jmp isr_common_stub

.global isr96
isr96:
    pushq $0
    pushq $96
    jmp isr_common_stub

.global isr97
isr97:
    pushq $0
    pushq $97
    jmp isr_common_stub

.global isr98
isr98:
    pushq $0
    pushq $98
    jmp isr_common_stub

.global isr99
isr99:
    pushq $0
    pushq $99
    jmp isr_common_stub

.global isr100
isr100:
    pushq $0
    pushq $100
    jmp isr_common_stub

.global isr101
isr101:
    pushq $0
    pushq $101
    jmp isr_common_stub

.global isr102
isr102:
    pushq $0
    pushq $102
    jmp isr_common_stub

.global isr103
isr103:
    pushq $0
    pushq $103
    jmp isr_common_stub

.global isr104
isr104:
    pushq $0
    pushq $104
    jmp isr_common_stub

.global isr105
isr105:
    pushq $0
    pushq $105
    jmp isr_common_stub

.global isr106
isr106:
    pushq $0
    pushq $106
    jmp isr_common_stub

.global isr107
isr107:
    pushq $0
    pushq $107
    jmp isr_common_stub

.global isr108
isr108:
    pushq $0
    pushq $108
    jmp isr_common_stub

.global isr109
isr109:
    pushq $0
    pushq $109
    jmp isr_common_stub

.global isr110
isr110:
    pushq $0
    pushq $110
    jmp isr_common_stub

.global isr111
isr111:
    pushq $0
    pushq $111
    jmp isr_common_stub

.global isr112
isr112:
    pushq $0
    pushq $112
    jmp isr_common_stub

.global isr113
isr113:
    pushq $0
    pushq $113
    jmp isr_common_stub

.global isr114
isr114:
    pushq $0
    pushq $114
    jmp isr_common_stub

.global isr115
isr115:
    pushq $0
    pushq $115
    jmp isr_common_stub

.global isr116
isr116:
    pushq $0
    pushq $116
    jmp isr_common_stub

.global isr117
isr117:
    pushq $0
    pushq $117
    jmp isr_common_stub

.global isr118
isr118:
    pushq $0
    pushq $118
    jmp isr_common_stub

.global isr119
isr119:
    pushq $0
    pushq $119
    jmp isr_common_stub

.global isr120
isr120:
    pushq $0
    pushq $120
    jmp isr_common_stub

.global isr121
isr121:
    pushq $0
    pushq $121
    jmp isr_common_stub

.global isr122
isr122:
    pushq $0
    pushq $122
    jmp isr_common_stub

.global isr123
isr123:
    pushq $0
    pushq $123
    jmp isr_common_stub

.global isr124
isr124:
    pushq $0
    pushq $124
    jmp isr_common_stub

.global isr125
isr125:
    pushq $0
    pushq $125
    jmp isr_common_stub

.global isr126
isr126:
    pushq $0
    pushq $126
    jmp isr_common_stub

.global isr127
isr127:
    pushq $0
    pushq $127
    jmp isr_common_stub

.global isr128
isr128:
    pushq $0
    pushq $128
    jmp isr_common_stub

.global isr129
isr129:
    pushq $0
    pushq $129
    jmp isr_common_stub

.global isr130
isr130:
    pushq $0
    pushq $130
    jmp isr_common_stub

.global isr131
isr131:
    pushq $0
    pushq $131
    jmp isr_common_stub

.global isr132
isr132:
    pushq $0
    pushq $132
    jmp isr_common_stub

.global isr133
isr133:
    pushq $0
    pushq $133
    jmp isr_common_stub

.global isr134
isr134:
    pushq $0
    pushq $134
    jmp isr_common_stub

.global isr135
isr135:
    pushq $0
    pushq $135
    jmp isr_common_stub

.global isr136
isr136:
    pushq $0
    pushq $136
    jmp isr_common_stub

.global isr137
isr137:
    pushq $0
    pushq $137
    jmp isr_common_stub

.global isr138
isr138:
    pushq $0
    pushq $138
    jmp isr_common_stub

.global isr139
isr139:
    pushq $0
    pushq $139
    jmp isr_common_stub

.global isr140
isr140:
    pushq $0
    pushq $140
    jmp isr_common_stub

.global isr141
isr141:
    pushq $0
    pushq $141
    jmp isr_common_stub

.global isr142
isr142:
    pushq $0
    pushq $142
    jmp isr_common_stub

.global isr143
isr143:
    pushq $0
    pushq $143
    jmp isr_common_stub

.global isr144
isr144:
    pushq $0
    pushq $144
    jmp isr_common_stub

.global isr145
isr145:
    pushq $0
    pushq $145
    jmp isr_common_stub

.global isr146
isr146:
    pushq $0
    pushq $146
    jmp isr_common_stub

.global isr147
isr147:
    pushq $0
    pushq $147
    jmp isr_common_stub

.global isr148
isr148:
    pushq $0
    pushq $148
    jmp isr_common_stub

.global isr149
isr149:
    pushq $0
    pushq $149
    jmp isr_common_stub

.global isr150
isr150:
    pushq $0
    pushq $150
    jmp isr_common_stub

.global isr151
isr151:
    pushq $0
    pushq $151
    jmp isr_common_stub

.global isr152
isr152:
    pushq $0
    pushq $152
    jmp isr_common_stub

.global isr153
isr153:
    pushq $0
    pushq $153
    jmp isr_common_stub

.global isr154
isr154:
    pushq $0
    pushq $154
    jmp isr_common_stub

.global isr155
isr155:
    pushq $0
    pushq $155
    jmp isr_common_stub

.global isr156
isr156:
    pushq $0
    pushq $156
    jmp isr_common_stub

.global isr157
isr157:
    pushq $0
    pushq $157
    jmp isr_common_stub

.global isr158
isr158:
    pushq $0
    pushq $158
    jmp isr_common_stub

.global isr159
isr159:
    pushq $0
    pushq $159
    jmp isr_common_stub

.global isr160
isr160:
    pushq $0
    pushq $160
    jmp isr_common_stub

.global isr161
isr161:
    pushq $0
    pushq $161
    jmp isr_common_stub

.global isr162
isr162:
    pushq $0
    pushq $162
    jmp isr_common_stub

.global isr163
isr163:
    pushq $0
    pushq $163
    jmp isr_common_stub

.global isr164
isr164:
    pushq $0
    pushq $164
    jmp isr_common_stub

.global isr165
isr165:
    pushq $0
    pushq $165
    jmp isr_common_stub

.global isr166
isr166:
    pushq $0
    pushq $166
    jmp isr_common_stub

.global isr167
isr167:
    pushq $0
    pushq $167
    jmp isr_common_stub

.global isr168
isr168:
    pushq $0
    pushq $168
    jmp isr_common_stub

.global isr169
isr169:
    pushq $0
    pushq $169
    jmp isr_common_stub

.global isr170
isr170:
    pushq $0
    pushq $170
    jmp isr_common_stub

.global isr171
isr171:
    pushq $0
    pushq $171
    jmp isr_common_stub

.global isr172
isr172:
    pushq $0
    pushq $172
    jmp isr_common_stub

.global isr173
isr173:
    pushq $0
    pushq $173
    jmp isr_common_stub

.global isr174
isr174:
    pushq $0
    pushq $174
    jmp isr_common_stub

.global isr175
isr175:
    pushq $0
    pushq $175
    jmp isr_common_stub

.global isr176
isr176:
    pushq $0
    pushq $176
    jmp isr_common_stub

.global isr177
isr177:
    pushq $0
    pushq $177
    jmp isr_common_stub

.global isr178
isr178:
    pushq $0
    pushq $178
    jmp isr_common_stub

.global isr179
isr179:
    pushq $0
    pushq $179
    jmp isr_common_stub

.global isr180
isr180:
    pushq $0
    pushq $180
    jmp isr_common_stub

.global isr181
isr181:
    pushq $0
    pushq $181
    jmp isr_common_stub

.global isr182
isr182:
    pushq $0
    pushq $182
    jmp isr_common_stub

.global isr183
isr183:
    pushq $0
    pushq $183
    jmp isr_common_stub

.global isr184
isr184:
    pushq $0
    pushq $184
    jmp isr_common_stub

.global isr185
isr185:
    pushq $0
    pushq $185
    jmp isr_common_stub

.global isr186
isr186:
    pushq $0
    pushq $186
    jmp isr_common_stub

.global isr187
isr187:
    pushq $0
    pushq $187
    jmp isr_common_stub

.global isr188
isr188:
    pushq $0
    pushq $188
    jmp isr_common_stub

.global isr189
isr189:
    pushq $0
    pushq $189
    jmp isr_common_stub

.global isr190
isr190:
    pushq $0
    pushq $190
    jmp isr_common_stub

.global isr191
isr191:
    pushq $0
    pushq $191
    jmp isr_common_stub

.global isr192
isr192:
    pushq $0
    pushq $192
    jmp isr_common_stub

.global isr193
isr193:
    pushq $0
    pushq $193
    jmp isr_common_stub

.global isr194
isr194:
    pushq $0
    pushq $194
    jmp isr_common_stub

.global isr195
isr195:
    pushq $0
    pushq $195
    jmp isr_common_stub

.global isr196
isr196:
    pushq $0
    pushq $196
    jmp isr_common_stub

.global isr197
isr197:
    pushq $0
    pushq $197
    jmp isr_common_stub

.global isr198
isr198:
    pushq $0
    pushq $198
    jmp isr_common_stub

.global isr199
isr199:
    pushq $0
    pushq $199
    jmp isr_common_stub

.global isr200
isr200:
    pushq $0
    pushq $200
    jmp isr_common_stub

.global isr201
isr201:
    pushq $0
    pushq $201
    jmp isr_common_stub

.global isr202
isr202:
    pushq $0
    pushq $202
    jmp isr_common_stub

.global isr203
isr203:
    pushq $0
    pushq $203
    jmp isr_common_stub

.global isr204
isr204:
    pushq $0
    pushq $204
    jmp isr_common_stub

.global isr205
isr205:
    pushq $0
    pushq $205
    jmp isr_common_stub

.global isr206
isr206:
    pushq $0
    pushq $206
    jmp isr_common_stub

.global isr207
isr207:
    pushq $0
    pushq $207
    jmp isr_common_stub

.global isr208
isr208:
    pushq $0
    pushq $208
    jmp isr_common_stub

.global isr209
isr209:
    pushq $0
    pushq $209
    jmp isr_common_stub

.global isr210
isr210:
    pushq $0
    pushq $210
    jmp isr_common_stub

.global isr211
isr211:
    pushq $0
    pushq $211
    jmp isr_common_stub

.global isr212
isr212:
    pushq $0
    pushq $212
    jmp isr_common_stub

.global isr213
isr213:
    pushq $0
    pushq $213
    jmp isr_common_stub

.global isr214
isr214:
    pushq $0
    pushq $214
    jmp isr_common_stub

.global isr215
isr215:
    pushq $0
    pushq $215
    jmp isr_common_stub

.global isr216
isr216:
    pushq $0
    pushq $216
    jmp isr_common_stub

.global isr217
isr217:
    pushq $0
    pushq $217
    jmp isr_common_stub

.global isr218
isr218:
    pushq $0
    pushq $218
    jmp isr_common_stub

.global isr219
isr219:
    pushq $0
    pushq $219
    jmp isr_common_stub

.global isr220
isr220:
    pushq $0
    pushq $220
    jmp isr_common_stub

.global isr221
isr221:
    pushq $0
    pushq $221
    jmp isr_common_stub

.global isr222
isr222:
    pushq $0
    pushq $222
    jmp isr_common_stub

.global isr223
isr223:
    pushq $0
    pushq $223
    jmp isr_common_stub

.global isr224
isr224:
    pushq $0
    pushq $224
    jmp isr_common_stub

.global isr225
isr225:
    pushq $0
    pushq $225
    jmp isr_common_stub

.global isr226
isr226:
    pushq $0
    pushq $226
    jmp isr_common_stub

.global isr227
isr227:
    pushq $0
    pushq $227
    jmp isr_common_stub

.global isr228
isr228:
    pushq $0
    pushq $228
    jmp isr_common_stub

.global isr229
isr229:
    pushq $0
    pushq $229
    jmp isr_common_stub

.global isr230
isr230:
    pushq $0
    pushq $230
    jmp isr_common_stub

.global isr231
isr231:
    pushq $0
    pushq $231
    jmp isr_common_stub

.global isr232
isr232:
    pushq $0
    pushq $232
    jmp isr_common_stub

.global isr233
isr233:
    pushq $0
    pushq $233
    jmp isr_common_stub

.global isr234
isr234:
    pushq $0
    pushq $234
    jmp isr_common_stub

.global isr235
isr235:
    pushq $0
    pushq $235
    jmp isr_common_stub

.global isr236
isr236:
    pushq $0
    pushq $236
    jmp isr_common_stub

.global isr237
isr237:
    pushq $0
    pushq $237
    jmp isr_common_stub

.global isr238
isr238:
    pushq $0
    pushq $238
    jmp isr_common_stub

.global isr239
isr239:
    pushq $0
    pushq $239
    jmp isr_common_stub

.global isr240
isr240:
    pushq $0
    pushq $240
    jmp isr_common_stub

.global isr241
isr241:
    pushq $0
    pushq $241
    jmp isr_common_stub

.global isr242
isr242:
    pushq $0
    pushq $242
    jmp isr_common_stub

.global isr243
isr243:
    pushq $0
    pushq $243
    jmp isr_common_stub

.global isr244
isr244:
    pushq $0
    pushq $244
    jmp isr_common_stub

.global isr245
isr245:
    pushq $0
    pushq $245
    jmp isr_common_stub

.global isr246
isr246:
    pushq $0
    pushq $246
    jmp isr_common_stub

.global isr247
isr247:
    pushq $0
    pushq $247
    jmp isr_common_stub

.global isr248
isr248:
    pushq $0
    pushq $248
    jmp isr_common_stub

.global isr249
isr249:
    pushq $0
    pushq $249
    jmp isr_common_stub

.global isr250
isr250:
    pushq $0
    pushq $250
    jmp isr_common_stub

.global isr251
isr251:
    pushq $0
    pushq $251
    jmp isr_common_stub

.global isr252
isr252:
    pushq $0
    pushq $252
    jmp isr_common_stub

.global isr253
isr253:
    pushq $0
    pushq $253
    jmp isr_common_stub

.global isr254
isr254:
    pushq $0
    pushq $254
    jmp isr_common_stub

.global isr255
isr255:
    pushq $0
    pushq $255
    jmp isr_common_stub

.section .rodata
.align 8
.global isr_stub_table
isr_stub_table:
    .quad isr0
    .quad isr1
    .quad isr2
    .quad isr3
    .quad isr4
    .quad isr5
    .quad isr6
    .quad isr7
    .quad isr8
    .quad isr9
    .quad isr10
    .quad isr11
    .quad isr12
    .quad isr13
    .quad isr14
    .quad isr15
    .quad isr16
    .quad isr17
    .quad isr18
    .quad isr19
    .quad isr20
    .quad isr21
    .quad isr22
    .quad isr23
    .quad isr24
    .quad isr25
    .quad isr26
    .quad isr27
    .quad isr28
    .quad isr29
    .quad isr30
    .quad isr31
    .quad isr32
    .quad isr33
    .quad isr34
    .quad isr35
    .quad isr36
    .quad isr37
    .quad isr38
    .quad isr39
    .quad isr40
    .quad isr41
    .quad isr42
    .quad isr43
    .quad isr44
    .quad isr45
    .quad isr46
    .quad isr47
    .quad isr48
    .quad isr49
    .quad isr50
    .quad isr51
    .quad isr52
    .quad isr53
    .quad isr54
    .quad isr55
    .quad isr56
    .quad isr57
    .quad isr58
    .quad isr59
    .quad isr60
    .quad isr61
    .quad isr62
    .quad isr63
    .quad isr64
    .quad isr65
    .quad isr66
    .quad isr67
    .quad isr68
    .quad isr69
    .quad isr70
    .quad isr71
    .quad isr72
    .quad isr73
    .quad isr74
    .quad isr75
    .quad isr76
    .quad isr77
    .quad isr78
    .quad isr79
    .quad isr80
    .quad isr81
    .quad isr82
    .quad isr83
    .quad isr84
    .quad isr85
    .quad isr86
    .quad isr87
    .quad isr88
    .quad isr89
    .quad isr90
    .quad isr91
    .quad isr92
    .quad isr93
    .quad isr94
    .quad isr95
    .quad isr96
    .quad isr97
    .quad isr98
    .quad isr99
    .quad isr100
    .quad isr101
    .quad isr102
    .quad isr103
    .quad isr104
    .quad isr105
    .quad isr106
    .quad isr107
    .quad isr108
    .quad isr109
    .quad isr110
    .quad isr111
    .quad isr112
    .quad isr113
    .quad isr114
    .quad isr115
    .quad isr116
    .quad isr117
    .quad isr118
    .quad isr119
    .quad isr120
    .quad isr121
    .quad isr122
    .quad isr123
    .quad isr124
    .quad isr125
    .quad isr126
    .quad isr127
    .quad isr128
    .quad isr129
    .quad isr130
    .quad isr131
    .quad isr132
    .quad isr133
    .quad isr134
    .quad isr135
    .quad isr136
    .quad isr137
    .quad isr138
    .quad isr139
    .quad isr140
    .quad isr141
    .quad isr142
    .quad isr143
    .quad isr144
    .quad isr145
    .quad isr146
    .quad isr147
    .quad isr148
    .quad isr149
    .quad isr150
    .quad isr151
    .quad isr152
    .quad isr153
    .quad isr154
    .quad isr155
    .quad isr156
    .quad isr157
    .quad isr158
    .quad isr159
    .quad isr160
    .quad isr161
    .quad isr162
    .quad isr163
    .quad isr164
    .quad isr165
    .quad isr166
    .quad isr167
    .quad isr168
    .quad isr169
    .quad isr170
    .quad isr171
    .quad isr172
    .quad isr173
    .quad isr174
    .quad isr175
    .quad isr176
    .quad isr177
    .quad isr178
    .quad isr179
    .quad isr180
    .quad isr181
    .quad isr182
    .quad isr183
    .quad isr184
    .quad isr185
    .quad isr186
    .quad isr187
    .quad isr188
    .quad isr189
    .quad isr190
    .quad isr191
    .quad isr192
    .quad isr193
    .quad isr194
    .quad isr195
    .quad isr196
    .quad isr197
    .quad isr198
    .quad isr199
    .quad isr200
    .quad isr201
    .quad isr202
    .quad isr203
    .quad isr204
    .quad isr205
    .quad isr206
    .quad isr207
    .quad isr208
    .quad isr209
    .quad isr210
    .quad isr211
    .quad isr212
    .quad isr213
    .quad isr214
    .quad isr215
    .quad isr216
    .quad isr217
    .quad isr218
    .quad isr219
    .quad isr220
    .quad isr221
    .quad isr222
    .quad isr223
    .quad isr224
    .quad isr225
    .quad isr226
    .quad isr227
    .quad isr228
    .quad isr229
    .quad isr230
    .quad isr231
    .quad isr232
    .quad isr233
    .quad isr234
    .quad isr235
    .quad isr236
    .quad isr237
    .quad isr238
    .quad isr239
    .quad isr240
    .quad isr241
    .quad isr242
    .quad isr243
    .quad isr244
    .quad isr245
    .quad isr246
    .quad isr247
    .quad isr248
    .quad isr249
    .quad isr250
    .quad isr251
    .quad isr252
    .quad isr253
    .quad isr254
    .quad isr255
