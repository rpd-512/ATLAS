module ALU_4bit_Complex(
    input  logic [3:0] A,
    input  logic [3:0] B,
    input  logic [2:0] Opcode,
    output logic [3:0] Result,
    output logic       CarryOut
);

    // Single-module logic containing mixed arithmetic, reduction, and bitwise branches
    always_comb begin
        Result   = 4'b0000;
        CarryOut = 1'b0;
        
        unique case (Opcode)
            3'b000: begin // Complex Arithmetic with mixed bit operations
                {CarryOut, Result} = (A & B) + (A | B);
            end
            3'b001: begin // Dynamic structural subtraction
                {CarryOut, Result} = A - B - 1'b1;
            end
            3'b010: begin // Logic Barrel Shift
                Result = A << B[1:0];
            end
            3'b011: begin // Population Count (Bitwise reduction logic density)
                Result = 4'(A[0] + A[1] + A[2] + A[3]);
            end
            3'b100: begin // Bit Reversal combined with conditional mask
                Result = {A[0], A[1], A[2], A[3]} ^ B;
            end
            3'b101: begin // Conditional Greater-Than arithmetic mapping
                Result = (A > B) ? (A & ~B) : (~A & B);
            end
            default: begin // Default structural fallback
                Result = ~(A ^ B);
            end
        endcase
    end

endmodule
